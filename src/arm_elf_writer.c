#include <elf.h>
#include <stdio.h>
#include <string.h>

#include "arm_elf_writer.h"

void write_arm_elf(const char *filename, const OutputBuffer *buf) {
    if (!buf) return;

    FILE *f = fopen(filename, "wb");
    if (!f) return;

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
    ehdr.e_machine = EM_ARM;     // ARM architecture
    ehdr.e_version = EV_CURRENT;
    ehdr.e_entry   = 0x8000;     // entry point virtual address
    ehdr.e_phoff   = sizeof(Elf32_Ehdr);
    ehdr.e_ehsize  = sizeof(Elf32_Ehdr);
    ehdr.e_phentsize = sizeof(Elf32_Phdr);
    ehdr.e_phnum   = 1;

    Elf32_Phdr phdr;
    memset(&phdr, 0, sizeof(phdr));
    phdr.p_type   = PT_LOAD;
    phdr.p_offset = 0x8000;      // file offset where code starts
    phdr.p_vaddr  = 0x8000;      // virtual address
    phdr.p_filesz = (Elf32_Word)buf->size;  // actual assembled code size
    phdr.p_memsz  = (Elf32_Word)buf->size;
    phdr.p_flags  = PF_X | PF_R;
    phdr.p_align  = 4;

    fwrite(&ehdr, 1, sizeof(ehdr), f);
    fwrite(&phdr, 1, sizeof(phdr), f);

    // pad up to 0x8000
    char pad[0x8000 - (sizeof(ehdr) + sizeof(phdr))];
    memset(pad, 0, sizeof(pad));
    fwrite(pad, 1, sizeof(pad), f);

    // write the assembled instruction bytes produced by emit_code()
    fwrite(buf->data, 1, buf->size, f);

    fclose(f);
}
