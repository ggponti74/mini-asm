#include <string.h>
#include <stdio.h>

#include "arm_elf_writer.h"

void write_arm_elf(const char *filename, const OutputBuffer *buf) {
  if (!buf)
    return;

  const uint32_t code_addr = 0x8000;
  // ARM instructions must be word-aligned: odd-sized data (dc.b, dc.w) at the
  // end of the program would otherwise misalign the epilogue (Bus error).
  const uint32_t padded_size = ((uint32_t)buf->size + 3u) & ~3u;
  const uint32_t epilogue_addr = code_addr + padded_size;
  const uint32_t entry_addr = epilogue_addr;
  const uint32_t total_size = padded_size + EPILOGUE_SIZE;
  const uint32_t regfile_addr = REGFILE_ALIGN_UP(code_addr + total_size);

  FILE *f = fopen(filename, "wb");
  if (!f)
    return;

  Elf32_Ehdr ehdr;
  memset(&ehdr, 0, sizeof(ehdr));
  ehdr.e_ident[EI_MAG0] = ELFMAG0;
  ehdr.e_ident[EI_MAG1] = ELFMAG1;
  ehdr.e_ident[EI_MAG2] = ELFMAG2;
  ehdr.e_ident[EI_MAG3] = ELFMAG3;
  ehdr.e_ident[EI_CLASS] = ELFCLASS32;
  ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
  ehdr.e_ident[EI_VERSION] = EV_CURRENT;
  ehdr.e_type = ET_EXEC;
  ehdr.e_machine = EM_ARM; // ARM architecture
  ehdr.e_version = EV_CURRENT;
  ehdr.e_entry = entry_addr; // entry point virtual address
  ehdr.e_phoff = sizeof(Elf32_Ehdr);
  ehdr.e_ehsize = sizeof(Elf32_Ehdr);
  ehdr.e_phentsize = sizeof(Elf32_Phdr);
  ehdr.e_flags = 0x05000000; // EF_ARM_EABI_VER5
  ehdr.e_phnum = 1;

  Elf32_Phdr phdr;
  memset(&phdr, 0, sizeof(phdr));
  phdr.p_type = PT_LOAD;
  phdr.p_offset = 0x8000;     // file offset where code starts
  phdr.p_vaddr = 0x8000;      // virtual address
  phdr.p_paddr = 0x8000;      // physical address (match vaddr)
  phdr.p_filesz = total_size;
  phdr.p_memsz = REGFILE_ALIGN_UP(total_size) + REGFILE_SIZE;
  phdr.p_flags = PF_X | PF_R | PF_W;
  phdr.p_align = 0x1000;

  // write ELF headers

  fwrite(&ehdr, 1, sizeof(ehdr), f);
  fwrite(&phdr, 1, sizeof(phdr), f);

  // pad up to 0x8000
  char pad[0x8000 - (sizeof(ehdr) + sizeof(phdr))];
  memset(pad, 0, sizeof(pad));
  fwrite(pad, 1, sizeof(pad), f);

  // write the assembled instruction bytes produced by emit_code()
  fwrite(buf->data, 1, buf->size, f);
  for (size_t i = buf->size; i < padded_size; i++)
    fputc(0, f);

  // --- entry stub + epilogue (the ELF entry point is the first word) ---
  // Command line: the kernel starts the process with argc at [sp] and the
  // argv pointer array right above it (argv[0] = the command, then the
  // arguments, then a NULL). The stub gives the program D0 = argc and
  // A0 = &argv[0], which the 68K registers keep in the register block.
  // 68K memory is big-endian, so a program reading an argv entry with
  // MOVE.L (An) must get the pointer, not its byte-swapped value: the stub
  // therefore byte-swaps the pointers of the array in place (the NULL
  // terminator stays 0). The strings themselves are plain bytes.
  //    0 ldr   r12, [pc, #60]   ; r12 = register block
  //    1 ldr   r0, [sp]         ; argc
  //    2 str   r0, [r12]        ; D0 = argc
  //    3 add   r1, sp, #4       ; &argv[0]
  //    4 str   r1, [r12, #32]   ; A0 = &argv[0]
  //    5 mov   r2, r1
  //    6 ldr   r3, [r2]         ; loop: next argv entry
  //    7 cmp   r3, #0
  //    8 beq   12               ;   NULL: done
  //    9 rev   r3, r3
  //   10 str   r3, [r2], #4
  //   11 b     6
  //   12 bl    user_code
  //   13 ldr   r12, [pc, #8]    ; user code returned: exit(D0)
  //   14 ldr   r0, [r12]
  //   15 mov   r7, #1
  //   16 svc   0
  //   17 .word register block
  int32_t imm24 = ((int32_t)code_addr - (int32_t)(epilogue_addr + 12 * 4 + 8)) >> 2;
  uint32_t bl_instr = 0xEB000000u | ((uint32_t)imm24 & 0x00FFFFFFu);
  uint32_t epilogue[EPILOGUE_SIZE / 4] = {
      0xE59FC03Cu, 0xE59D0000u, 0xE58C0000u, 0xE28D1004u, 0xE58C1020u,
      0xE1A02001u, 0xE5923000u, 0xE3530000u, 0x0A000002u, 0xE6BF3F33u,
      0xE4823004u, 0xEAFFFFF9u, bl_instr,   0xE59FC008u, 0xE59C0000u,
      0xE3A07001u, 0xEF000000u, regfile_addr};
  for (int i = 0; i < EPILOGUE_SIZE / 4; i++) {
    uint8_t bytes[4] = {
        (uint8_t)(epilogue[i] & 0xFF),
        (uint8_t)((epilogue[i] >> 8) & 0xFF),
        (uint8_t)((epilogue[i] >> 16) & 0xFF),
        (uint8_t)((epilogue[i] >> 24) & 0xFF),
    };
    fwrite(bytes, 1, 4, f);
  }

  fclose(f);
}
