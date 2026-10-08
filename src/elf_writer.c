#include <string.h>
#include <stdio.h>

#include "elf_writer.h"

// write_elf: assembles a minimal, statically-linked x86 Linux ELF32
// executable containing the assembled code, followed by an epilogue
// that guarantees a clean exit(EAX) -- i.e. exit(D0), since D0 maps to
// EAX under this table's register numbering.
//
// The user's own code may end in a mnemonic like RTS (-> x86 RET), which
// is written as if returning to a caller. To make that work at the top
// level (where there's no caller — just whatever the kernel left on the
// stack at process start), the entry point is the epilogue, not the
// code: it CALLs into the code (pushing the epilogue's own address as
// the return address), the code runs and RETs, landing back right after
// the CALL. From there the epilogue saves off whatever the code left in
// EAX/D0 *before* clobbering EAX with the syscall number, so the
// process's real exit status is whatever the assembled program computed,
// not a hardcoded 0. Mirrors the BL-back-into-code trick write_arm_elf()
// uses for the same reason.
//
// Command line: the kernel starts a process with argc at [esp] and the
// argv pointer array right above it (argv[0] = the command itself, then
// the arguments, then a NULL). Before calling into the code, the entry
// stub copies those into the 68K registers: D0 (EAX) = argc and
// A0 (ESI) = pointer to argv, an array of 32-bit pointers to
// NUL-terminated strings, argv[0] being the command and the array
// ending with a 0 pointer. Both count the command itself.
void write_elf(const char *filename, const OutputBuffer *buf) {
    if (!buf) return;

    FILE *f = fopen(filename, "wb");
    if (!f) return;

    const uint32_t base_addr = 0x08048000;
    const size_t header_size = sizeof(Elf32_Ehdr) + sizeof(Elf32_Phdr);
    (void)header_size; // headers are padded out to 0x1000 below, not packed tightly

    const uint32_t code_addr = base_addr + 0x1000;
    const uint32_t epilogue_addr = code_addr + (uint32_t)buf->size;
    const uint32_t total_size = (uint32_t)buf->size + ELF_EPILOGUE_SIZE +
                                CORE_ELF_DISPATCHER_SIZE;

    Elf32_Ehdr ehdr;
    memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[EI_MAG0] = ELFMAG0;
    ehdr.e_ident[EI_MAG1] = ELFMAG1;
    ehdr.e_ident[EI_MAG2] = ELFMAG2;
    ehdr.e_ident[EI_MAG3] = ELFMAG3;
    ehdr.e_ident[EI_CLASS] = ELFCLASS32;
    ehdr.e_ident[EI_DATA]  = ELFDATA2LSB;
    ehdr.e_ident[EI_VERSION] = EV_CURRENT;
    ehdr.e_type    = ET_EXEC;
    ehdr.e_machine = EM_386;   // x86 Linux
    ehdr.e_version = EV_CURRENT;
    ehdr.e_entry   = epilogue_addr;  // start in the epilogue, not the code
    ehdr.e_phoff   = sizeof(Elf32_Ehdr);
    ehdr.e_ehsize  = sizeof(Elf32_Ehdr);
    ehdr.e_phentsize = sizeof(Elf32_Phdr);
    ehdr.e_phnum   = 1;

    Elf32_Phdr phdr;
    memset(&phdr, 0, sizeof(phdr));
    phdr.p_type   = PT_LOAD;
    phdr.p_offset = 0x1000;
    phdr.p_vaddr  = code_addr;
    phdr.p_paddr  = code_addr;
    // Code + epilogue both live in this one PT_LOAD segment.
    // The emulated-register block follows the epilogue (4-byte aligned) and
    // exists only in memory (p_memsz > p_filesz, zero-filled by the kernel).
    // The segment is writable: 68K code and data share one flat space.
    phdr.p_filesz = total_size;
    phdr.p_memsz  = REGFILE_ALIGN_UP(total_size) + REGFILE_SIZE;
    phdr.p_flags  = PF_X | PF_W | PF_R;
    phdr.p_align  = 0x1000;

    // Write headers
    fwrite(&ehdr, 1, sizeof(ehdr), f);
    fwrite(&phdr, 1, sizeof(phdr), f);

    // Write the assembled instruction bytes produced by emit_code()
    fseek(f, 0x1000, SEEK_SET);
    fwrite(buf->data, 1, buf->size, f);

    // --- entry stub + epilogue ---
    // 68K memory is big-endian, so a program reading an argv entry with
    // MOVE.L (An) must get the pointer, not its byte-swapped value. The
    // stub therefore byte-swaps the pointers of the kernel's argv array in
    // place first (the NULL terminator stays 0; the strings are plain
    // bytes). It only touches EAX/ESI, which the next two instructions
    // overwrite, so D1-D7 keep their power-on value of 0.
    //    0 lea   esi,[esp+4]     ; &argv[0]
    //    4 mov   eax,[esi]       ; loop: next argv entry
    //    6 test  eax,eax
    //    8 jz    19              ;   NULL: done
    //   10 bswap eax
    //   12 mov   [esi],eax
    //   14 add   esi,4
    //   17 jmp   4
    //   19 mov   eax,[esp]       ; D0 <- argc (counts the command itself)
    //   22 lea   esi,[esp+4]     ; A0 <- &argv[0]
    //   26 call  code_addr       ; pushes the address of the "mov ebx,eax" below
    //   31 mov   ebx,eax         ; exit status <- D0 (before EAX gets the syscall no.)
    //   33 mov   eax,1           ; sys_exit
    //   38 int   0x80
    // D0/A0 are loaded right before the CALL: afterwards ESP has moved and
    // the user's code is free to change EAX/ESI anyway.
    const int32_t call_rel = (int32_t)code_addr - (int32_t)(epilogue_addr + 26 + 5);
    uint8_t epilogue[ELF_EPILOGUE_SIZE] = {
        0x8D, 0x74, 0x24, 0x04,         // lea esi, [esp+4]
        0x8B, 0x06,                     // mov eax, [esi]
        0x85, 0xC0,                     // test eax, eax
        0x74, 0x09,                     // jz  +9
        0x0F, 0xC8,                     // bswap eax
        0x89, 0x06,                     // mov [esi], eax
        0x83, 0xC6, 0x04,               // add esi, 4
        0xEB, 0xF1,                     // jmp -15
        0x8B, 0x04, 0x24,               // mov eax, [esp]
        0x8D, 0x74, 0x24, 0x04,         // lea esi, [esp+4]
        0xE8, 0x00, 0x00, 0x00, 0x00,   // call code_addr (rel32 patched below)
        0x89, 0xC3,                     // mov ebx, eax   (exit status <- D0)
        0xB8, 0x01, 0x00, 0x00, 0x00,   // mov eax, 1     (sys_exit)
        0xCD, 0x80                      // int 0x80
    };
    memcpy(&epilogue[27], &call_rel, sizeof(call_rel));
    fwrite(epilogue, 1, sizeof(epilogue), f);

    // --- TRAP #0 dispatcher, right after the epilogue (codegen CALLs it) ---
    uint8_t dispatcher[CORE_ELF_DISPATCHER_SIZE];
    int dn = core_emit_dispatcher("elf", dispatcher, sizeof(dispatcher));
    if (dn != CORE_ELF_DISPATCHER_SIZE) {
        fclose(f);
        return;
    }
    fwrite(dispatcher, 1, (size_t)dn, f);

    fclose(f);
}
