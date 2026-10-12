#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "pe_writer.h"
#include "core.h"

// Align to 512 bytes for file alignment
#define FILE_ALIGN 0x200
#define SEC_ALIGN 0x1000

// Minimal PE header structures
#pragma pack(push, 1)
typedef struct
{
    uint16_t e_magic; // "MZ"
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
    uint32_t e_lfanew; // Offset to PE header
} DOSHeader;

typedef struct
{
    uint32_t Signature; // "PE\0\0"
    uint16_t Machine;   // 0x14C = x86
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} COFFHeader;

typedef struct
{
    uint32_t VirtualAddress;
    uint32_t Size;
} IMAGE_DATA_DIRECTORY;

typedef struct
{
    uint16_t Magic; // 0x10B = PE32
    uint8_t MajorLinkerVersion;
    uint8_t MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint; // RVA
    uint32_t BaseOfCode;          // RVA
    uint32_t BaseOfData;          // RVA (PE32 only; absent in PE32+)
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

typedef struct
{
    uint8_t Name[8];
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

static uint32_t align_up(uint32_t value, uint32_t align)
{
    return (value + align - 1) & ~(align - 1);
}

// write_pe: assembles a complete minimal PE32 console executable
// (DOS stub + COFF header + optional header + one .text section)
// from the assembled code in buf, and writes it to filename.
//
// .text holds everything, in this order (all offsets from the section start):
//
//   0                  the assembled program (buf)
//   startup_off        entry stub (core.c): GetCommandLineA -> argc/argv,
//                      CALL program, ExitProcess(D0); 4-byte aligned
//   dispatcher_off     the TRAP #0 dispatcher (core.c)
//   import_off         the import block: kernel32.dll with the nine functions
//                      the stub and the dispatcher use (core.c)
//   regfile_off        REGFILE_SIZE bytes, the emulated-register block  } memory
//   area_off           CORE_PE_ARGS_AREA_SIZE bytes, the stub's scratch } only
//
// The first three and the import block are in the file; the last two exist
// only in memory (VirtualSize > SizeOfRawData, zero filled by the loader).
// 68K programs mix code and data, so .text is writable (RWX); that also lets
// the loader fill in the IAT. The same layout is described to codegen through
// platform.c (code_align, dispatcher_off, code_tail): main.c computes the
// dispatcher and register-block addresses from it before pass 2.
void write_pe(const char *filename, const OutputBuffer *buf)
{
    if (!buf)
        return;

    FILE *f = fopen(filename, "wb");
    if (!f)
        return;

    const uint32_t image_base = 0x00400000;

    const uint32_t headers_raw_size =
        (uint32_t)sizeof(DOSHeader) +
        (uint32_t)sizeof(COFFHeader) +
        (uint32_t)sizeof(IMAGE_OPTIONAL_HEADER32) +
        (uint32_t)sizeof(IMAGE_SECTION_HEADER); // 1 section
    const uint32_t size_of_headers = align_up(headers_raw_size, FILE_ALIGN);

    const uint32_t text_rva = SEC_ALIGN; // .text starts at first section alignment boundary

    // --- layout of .text (see above) ---
    const uint32_t startup_off = align_up((uint32_t)buf->size, 4);
    const uint32_t dispatcher_off = startup_off + CORE_PE_STARTUP_SIZE;
    const uint32_t import_off = dispatcher_off + CORE_PE_DISPATCHER_SIZE;
    const uint32_t file_len = import_off + CORE_PE_IMPORT_SIZE;
    const uint32_t regfile_off = REGFILE_ALIGN_UP(file_len);
    const uint32_t area_off = regfile_off + REGFILE_SIZE;

    const uint32_t code_raw_size = align_up(file_len, FILE_ALIGN);
    const uint32_t text_virtual_size = area_off + CORE_PE_ARGS_AREA_SIZE;
    const uint32_t code_virtual_size = align_up(text_virtual_size, SEC_ALIGN);
    const uint32_t size_of_image = align_up(SEC_ALIGN, SEC_ALIGN) + code_virtual_size; // headers page + code page(s)

    const uint32_t import_rva = text_rva + import_off;

    // --- DOS header ---
    DOSHeader dos = {0};
    dos.e_magic = 0x5A4D;             // "MZ"
    dos.e_lfanew = sizeof(DOSHeader); // PE header starts right after (no DOS stub code)
    fwrite(&dos, sizeof(dos), 1, f);

    // --- COFF header ---
    COFFHeader coff = {0};
    coff.Signature = 0x00004550; // "PE\0\0"
    coff.Machine = 0x14C;        // IMAGE_FILE_MACHINE_I386
    coff.NumberOfSections = 1;
    coff.TimeDateStamp = 0;
    coff.PointerToSymbolTable = 0;
    coff.NumberOfSymbols = 0;
    coff.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
    coff.Characteristics = 0x0102; // EXECUTABLE_IMAGE | 32BIT_MACHINE
    fwrite(&coff, sizeof(coff), 1, f);

    // --- Optional header ---
    IMAGE_OPTIONAL_HEADER32 opt = {0};
    opt.Magic = 0x10B; // PE32
    opt.SizeOfCode = code_raw_size;
    opt.AddressOfEntryPoint = text_rva + startup_off; // the entry stub, which CALLs the program
    opt.BaseOfCode = text_rva;
    opt.BaseOfData = 0;
    opt.ImageBase = image_base;
    opt.SectionAlignment = SEC_ALIGN;
    opt.FileAlignment = FILE_ALIGN;
    opt.MajorOperatingSystemVersion = 5;
    opt.MinorOperatingSystemVersion = 1;
    opt.MajorSubsystemVersion = 5;
    opt.MinorSubsystemVersion = 1;
    opt.SizeOfImage = size_of_image;
    opt.SizeOfHeaders = size_of_headers;
    opt.Subsystem = 3; // IMAGE_SUBSYSTEM_WINDOWS_CUI
    opt.SizeOfStackReserve = 0x100000;
    opt.SizeOfStackCommit = 0x1000;
    opt.SizeOfHeapReserve = 0x100000;
    opt.SizeOfHeapCommit = 0x1000;
    opt.NumberOfRvaAndSizes = 16;
    opt.DataDirectory[1].VirtualAddress = import_rva;                       // import table
    opt.DataDirectory[1].Size = 2 * 20;                                     // kernel32 + the null descriptor
    opt.DataDirectory[12].VirtualAddress = import_rva + CORE_PE_IAT_OFFSET; // IAT
    opt.DataDirectory[12].Size = CORE_PE_IAT_SIZE;
    fwrite(&opt, sizeof(opt), 1, f);

    // --- Section header: .text ---
    IMAGE_SECTION_HEADER text = {0};
    memcpy(text.Name, ".text", 5); // remaining bytes stay zero-padded
    text.VirtualSize = text_virtual_size;
    text.VirtualAddress = text_rva;
    text.SizeOfRawData = code_raw_size;
    text.PointerToRawData = size_of_headers;
    text.Characteristics = 0xE0000020; // CODE | MEM_EXECUTE | MEM_READ | MEM_WRITE
    fwrite(&text, sizeof(text), 1, f);

    // --- Pad headers out to FileAlignment ---
    long pos = ftell(f);
    while (pos < (long)size_of_headers)
    {
        fputc(0, f);
        pos++;
    }

    // --- .text: program | entry stub | dispatcher | import block, padded to SizeOfRawData ---
    uint8_t startup[CORE_PE_STARTUP_SIZE];
    uint8_t dispatcher[CORE_PE_DISPATCHER_SIZE];
    uint8_t imports[CORE_PE_IMPORT_SIZE];
    const uint32_t text_va = image_base + text_rva;
    const uint32_t import_va = image_base + import_rva;
    const uint32_t regfile_va = text_va + regfile_off;
    if (core_pe_emit_startup(startup, sizeof startup, text_va + startup_off, text_va,
                             import_va + CORE_PE_IAT_OFFSET, text_va + area_off) != CORE_PE_STARTUP_SIZE ||
        core_pe_emit_dispatcher(dispatcher, sizeof dispatcher, import_va) != CORE_PE_DISPATCHER_SIZE ||
        core_pe_emit_imports(imports, sizeof imports, import_rva) != CORE_PE_IMPORT_SIZE)
    {
        fprintf(stderr, "internal error: could not build the PE entry stub, dispatcher or import block\n");
        fclose(f);
        return;
    }
    (void)regfile_va; // the register block is addressed by codegen (main.c computes the same address)

    fwrite(buf->data, 1, buf->size, f);
    for (uint32_t i = (uint32_t)buf->size; i < startup_off; i++)
        fputc(0, f);
    fwrite(startup, 1, sizeof startup, f);
    fwrite(dispatcher, 1, sizeof dispatcher, f);
    fwrite(imports, 1, sizeof imports, f);
    for (uint32_t i = file_len; i < code_raw_size; i++)
    {
        fputc(0, f);
    }

    fclose(f);
}

// Minimal pe_begin: writes DOS stub + PE signature
void pe_begin(FILE *f)
{
    DOSHeader dos = {0};
    dos.e_magic = 0x5A4D;             // "MZ"
    dos.e_lfanew = sizeof(DOSHeader); // offset to PE header

    fwrite(&dos, sizeof(dos), 1, f);

    COFFHeader coff = {0};
    coff.Signature = 0x00004550;   // "PE\0\0"
    coff.Machine = 0x14C;          // x86
    coff.NumberOfSections = 0;     // will fill later
    coff.SizeOfOptionalHeader = 0; // minimal stub
    coff.Characteristics = 0x0102; // executable, 32-bit

    fwrite(&coff, sizeof(coff), 1, f);
}

// Finalize the PE file: flush buffers and close
void pe_end(FILE *f)
{
    if (!f)
        return;

    // In a full implementation you’d:
    // - Patch SizeOfImage in the optional header
    // - Update NumberOfSections in the COFF header
    // - Align file size to FILE_ALIGN
    // For now, just flush and close.

    fflush(f);
    fclose(f);
}