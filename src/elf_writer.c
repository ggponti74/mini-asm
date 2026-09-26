#include "elf_writer.h"
#include <stdio.h>
#include <string.h>

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
void write_elf(const char *filename, const OutputBuffer *buf) {
    if (!buf) return;

    FILE *f = fopen(filename, "wb");
    if (!f) return;

    const uint32_t base_addr = 0x08048000;
    const size_t header_size = sizeof(Elf32_Ehdr) + sizeof(Elf32_Phdr);
    (void)header_size; // headers are padded out to 0x1000 below, not packed tightly

    const uint32_t code_addr = base_addr + 0x1000;
    const uint32_t epilogue_addr = code_addr + (uint32_t)buf->size;
    const uint32_t total_size = (uint32_t)buf->size + ELF_EPILOGUE_SIZE;

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
    phdr.p_filesz = total_size;
    phdr.p_memsz  = total_size;
    phdr.p_flags  = PF_X | PF_R;
    phdr.p_align  = 0x1000;

    // Write headers
    fwrite(&ehdr, 1, sizeof(ehdr), f);
    fwrite(&phdr, 1, sizeof(phdr), f);

    // Write the assembled instruction bytes produced by emit_code()
    fseek(f, 0x1000, SEEK_SET);
    fwrite(buf->data, 1, buf->size, f);

    // --- epilogue: call code_addr ; mov ebx,eax ; mov eax,1 ; int 0x80 ---
    // The CALL pushes (address of the "mov ebx,eax" below) as the return
    // address and jumps into the user's code. When the user's code RETs,
    // it lands right back here, saves EAX/D0 into EBX (the sys_exit exit
    // status register) *before* EAX gets overwritten with the syscall
    // number, and falls into sys_exit(D0).
    const int32_t call_rel = (int32_t)code_addr - (int32_t)(epilogue_addr + 5);
    uint8_t epilogue[ELF_EPILOGUE_SIZE] = {
        0xE8, 0x00, 0x00, 0x00, 0x00,   // call code_addr (rel32 patched below)
        0x89, 0xC3,                     // mov ebx, eax   (exit status <- D0)
        0xB8, 0x01, 0x00, 0x00, 0x00,   // mov eax, 1     (sys_exit)
        0xCD, 0x80                      // int 0x80
    };
    memcpy(&epilogue[1], &call_rel, sizeof(call_rel));
    fwrite(epilogue, 1, sizeof(epilogue), f);

    fclose(f);
}
