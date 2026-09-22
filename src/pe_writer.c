#include <stdio.h>
#include <stdint.h>

#include "pe_writer.h"

// Align to 512 bytes for file alignment
#define FILE_ALIGN 0x200
#define SEC_ALIGN  0x1000

// Minimal PE header structures
#pragma pack(push,1)
typedef struct {
    uint16_t e_magic;      // "MZ"
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    uint32_t e_lfanew;     // Offset to PE header
} DOSHeader;

typedef struct {
    uint32_t Signature;    // "PE\0\0"
    uint16_t Machine;      // 0x14C = x86
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} COFFHeader;
#pragma pack(pop)

void write_pe(const char *filename, const OutputBuffer *buf)
{}

// Minimal pe_begin: writes DOS stub + PE signature
void pe_begin(FILE *f) {
    DOSHeader dos = {0};
    dos.e_magic = 0x5A4D;   // "MZ"
    dos.e_lfanew = sizeof(DOSHeader); // offset to PE header

    fwrite(&dos, sizeof(dos), 1, f);

    COFFHeader coff = {0};
    coff.Signature = 0x00004550; // "PE\0\0"
    coff.Machine = 0x14C;        // x86
    coff.NumberOfSections = 0;   // will fill later
    coff.SizeOfOptionalHeader = 0; // minimal stub
    coff.Characteristics = 0x0102; // executable, 32-bit

    fwrite(&coff, sizeof(coff), 1, f);
}

// Finalize the PE file: flush buffers and close
void pe_end(FILE *f) {
    if (!f) return;

    // In a full implementation you’d:
    // - Patch SizeOfImage in the optional header
    // - Update NumberOfSections in the COFF header
    // - Align file size to FILE_ALIGN
    // For now, just flush and close.

    fflush(f);
    fclose(f);
}