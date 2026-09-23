#include <stdio.h>
#include <stdint.h>
#include <string.h>

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

typedef struct {
    uint32_t VirtualAddress;
    uint32_t Size;
} IMAGE_DATA_DIRECTORY;

typedef struct {
    uint16_t Magic;                    // 0x10B = PE32
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;      // RVA
    uint32_t BaseOfCode;               // RVA
    uint32_t BaseOfData;               // RVA (PE32 only; absent in PE32+)
    uint32_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint32_t SizeOfStackReserve;
    uint32_t SizeOfStackCommit;
    uint32_t SizeOfHeapReserve;
    uint32_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    IMAGE_DATA_DIRECTORY DataDirectory[16];
} IMAGE_OPTIONAL_HEADER32;

typedef struct {
    uint8_t  Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
} IMAGE_SECTION_HEADER;
#pragma pack(pop)

static uint32_t align_up(uint32_t value, uint32_t align) {
    return (value + align - 1) & ~(align - 1);
}

// write_pe: assembles a complete minimal PE32 console executable
// (DOS stub + COFF header + optional header + one .text section)
// from the assembled code in buf, and writes it to filename.
//
// Note: this produces a structurally valid, loadable PE32 image with
// a single executable section. It does not link against any Windows
// import libraries (no IAT/import table), so code beyond a bare
// RET-style instruction (anything that needs kernel32 imports, e.g.
// ExitProcess) will need import-table support added separately.
void write_pe(const char *filename, const OutputBuffer *buf)
{
    if (!buf) return;

    FILE *f = fopen(filename, "wb");
    if (!f) return;

    const uint32_t image_base = 0x00400000;

    const uint32_t headers_raw_size =
        (uint32_t)sizeof(DOSHeader) +
        (uint32_t)sizeof(COFFHeader) +
        (uint32_t)sizeof(IMAGE_OPTIONAL_HEADER32) +
        (uint32_t)sizeof(IMAGE_SECTION_HEADER);   // 1 section
    const uint32_t size_of_headers = align_up(headers_raw_size, FILE_ALIGN);

    const uint32_t code_raw_size = align_up((uint32_t)buf->size, FILE_ALIGN);
    const uint32_t code_virtual_size = align_up((uint32_t)buf->size, SEC_ALIGN);

    const uint32_t text_rva = SEC_ALIGN;            // .text starts at first section alignment boundary
    const uint32_t size_of_image = align_up(SEC_ALIGN, SEC_ALIGN) + code_virtual_size; // headers page + code page(s)

    // --- DOS header ---
    DOSHeader dos = {0};
    dos.e_magic  = 0x5A4D;              // "MZ"
    dos.e_lfanew = sizeof(DOSHeader);   // PE header starts right after (no DOS stub code)
    fwrite(&dos, sizeof(dos), 1, f);

    // --- COFF header ---
    COFFHeader coff = {0};
    coff.Signature            = 0x00004550; // "PE\0\0"
    coff.Machine              = 0x14C;      // IMAGE_FILE_MACHINE_I386
    coff.NumberOfSections     = 1;
    coff.TimeDateStamp        = 0;
    coff.PointerToSymbolTable = 0;
    coff.NumberOfSymbols      = 0;
    coff.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
    coff.Characteristics      = 0x0102;     // EXECUTABLE_IMAGE | 32BIT_MACHINE
    fwrite(&coff, sizeof(coff), 1, f);

    // --- Optional header ---
    IMAGE_OPTIONAL_HEADER32 opt = {0};
    opt.Magic                       = 0x10B; // PE32
    opt.SizeOfCode                  = code_raw_size;
    opt.AddressOfEntryPoint         = text_rva;   // code starts at the top of .text
    opt.BaseOfCode                  = text_rva;
    opt.BaseOfData                  = 0;
    opt.ImageBase                   = image_base;
    opt.SectionAlignment            = SEC_ALIGN;
    opt.FileAlignment               = FILE_ALIGN;
    opt.MajorOperatingSystemVersion = 5;
    opt.MinorOperatingSystemVersion = 1;
    opt.MajorSubsystemVersion       = 5;
    opt.MinorSubsystemVersion       = 1;
    opt.SizeOfImage                 = size_of_image;
    opt.SizeOfHeaders                = size_of_headers;
    opt.Subsystem                   = 3;    // IMAGE_SUBSYSTEM_WINDOWS_CUI
    opt.SizeOfStackReserve           = 0x100000;
    opt.SizeOfStackCommit            = 0x1000;
    opt.SizeOfHeapReserve            = 0x100000;
    opt.SizeOfHeapCommit             = 0x1000;
    opt.NumberOfRvaAndSizes          = 16;
    fwrite(&opt, sizeof(opt), 1, f);

    // --- Section header: .text ---
    IMAGE_SECTION_HEADER text = {0};
    memcpy(text.Name, ".text", 5);          // remaining bytes stay zero-padded
    text.VirtualSize     = (uint32_t)buf->size;
    text.VirtualAddress  = text_rva;
    text.SizeOfRawData   = code_raw_size;
    text.PointerToRawData = size_of_headers;
    text.Characteristics = 0x60000020;      // CODE | MEM_EXECUTE | MEM_READ
    fwrite(&text, sizeof(text), 1, f);

    // --- Pad headers out to FileAlignment ---
    long pos = ftell(f);
    while (pos < (long)size_of_headers) {
        fputc(0, f);
        pos++;
    }

    // --- Code section, padded to SizeOfRawData ---
    fwrite(buf->data, 1, buf->size, f);
    for (uint32_t i = (uint32_t)buf->size; i < code_raw_size; i++) {
        fputc(0, f);
    }

    fclose(f);
}

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