#include "elf_writer.h"
#include <stdio.h>
#include <string.h>

void write_elf(const char *filename, const OutputBuffer *buf) {
    FILE *f = fopen(filename, "wb");
    if (!f) return;

    const uint32_t base_addr = 0x08048000;
    const size_t header_size = sizeof(Elf32_Ehdr) + sizeof(Elf32_Phdr);

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
    ehdr.e_entry   = base_addr + header_size; // entry = start of code
    ehdr.e_phoff   = sizeof(Elf32_Ehdr);
    ehdr.e_ehsize  = sizeof(Elf32_Ehdr);
    ehdr.e_phentsize = sizeof(Elf32_Phdr);
    ehdr.e_phnum   = 1;

    Elf32_Phdr phdr;
    memset(&phdr, 0, sizeof(phdr));
    phdr.p_type   = PT_LOAD;
    //phdr.p_offset = header_size;             // file offset of code
    //phdr.p_vaddr  = base_addr + header_size; // virtual address of code
    phdr.p_filesz = buf->size;
    phdr.p_memsz  = buf->size;
    phdr.p_flags  = PF_X | PF_R;
    phdr.p_align  = 0x1000;

    phdr.p_offset = 0x1000;
    phdr.p_vaddr  = base_addr + 0x1000;
    ehdr.e_entry  = phdr.p_vaddr;


    // Write headers
    fwrite(&ehdr, 1, sizeof(ehdr), f);
    fwrite(&phdr, 1, sizeof(phdr), f);

    // Write code immediately after headers
    fseek(f, 0x1000, SEEK_SET);
    fwrite(buf->data, 1, buf->size, f);

    fclose(f);
}
