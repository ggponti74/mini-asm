<<<<<<< HEAD
=======
#include <elf.h>

>>>>>>> 234ca1715fcc8cbf884357847ac3bad88711f040
#include <stdio.h>
#include <string.h>

#include "arm_elf_writer.h"

void write_arm_elf(const char *filename, const OutputBuffer *buf) {
  if (!buf)
    return;

  const uint32_t code_addr = 0x8000;
  const uint32_t epilogue_addr = code_addr + (uint32_t)buf->size;
  const uint32_t entry_addr = epilogue_addr;
  const uint32_t total_size = (uint32_t)buf->size + EPILOGUE_SIZE;

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
  phdr.p_filesz = total_size; // actual assembled code size
  phdr.p_memsz = total_size;
  phdr.p_flags = PF_X | PF_R;
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

  // --- epilogue: BL user_code_start ; mov r7, #1 ; svc 0 ---
  int32_t imm24 = ((int32_t)code_addr - (int32_t)(epilogue_addr + 8)) >> 2;
  uint32_t bl_instr = 0xEB000000u | ((uint32_t)imm24 & 0x00FFFFFFu);
  uint32_t mov_instr = 0xE3A07001u; // mov r7, #1
  uint32_t svc_instr = 0xEF000000u; // svc 0

  uint32_t epilogue[3] = {bl_instr, mov_instr, svc_instr};
  for (int i = 0; i < 3; i++) {
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
