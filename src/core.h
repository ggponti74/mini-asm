#ifndef CORE_H
#define CORE_H

#include <stddef.h> // size_t
#include <stdint.h> // uint8_t

//void exit(void);
void write(void);
void read(void);

#define CORE_SVC_EXIT 1
#define CORE_SVC_WRITE 2
#define CORE_SVC_READ 3

#define CORE_ELF_DISPATCHER_SIZE 64  // core.c checks this at compile time
#define CORE_ARM_DISPATCHER_SIZE 120 // likewise (30 ARM instructions)

// Windows PE (x86): the dispatcher calls kernel32 through the import table, so
// it needs the table's address; use these two instead of core_emit_dispatcher().
#define CORE_PE_IMPORT_SIZE     200 // the import block (directory, ILT, IAT, names)
#define CORE_PE_IAT_OFFSET      68  // where the IAT sits inside the import block
#define CORE_PE_IAT_SIZE        28  // the IAT: 6 imports + the terminating zero
#define CORE_PE_DISPATCHER_SIZE 164 // the dispatcher itself
#define CORE_PE_STARTUP_SIZE    240 // the entry stub that builds argc/argv
#define CORE_PE_ARGS_AREA_SIZE  0x18010 // memory-only: 32 KB of strings + room for 16387 argv pointers

size_t core_dispatcher_size(const char *target); // 0 = unsupported target
int core_emit_dispatcher(const char *target, uint8_t *out, size_t cap);
// bytes written, or -1

// Windows PE: write the CORE_PE_IMPORT_SIZE-byte import block that goes at
// block_rva (an RVA), and the dispatcher, which calls through that block's IAT
// (block_va = image base + block_rva). Each returns the bytes written, or -1.
int core_pe_emit_imports(uint8_t *out, size_t cap, uint32_t block_rva);
int core_pe_emit_dispatcher(uint8_t *out, size_t cap, uint32_t block_va);

// Windows PE: the entry stub. It gets the command line from GetCommandLineA,
// splits it into argv (the documented MSVC rules), gives the program D0 = argc
// and A0 = argv like the other targets do, runs the program (a CALL to
// user_va) and passes its D0 to ExitProcess. startup_va = the stub's own
// address, iat_va = the import block's IAT, area_va = a CORE_PE_ARGS_AREA_SIZE
// byte scratch area (all absolute addresses). Returns the bytes written, or -1.
int core_pe_emit_startup(uint8_t *out, size_t cap, uint32_t startup_va, uint32_t user_va,
                         uint32_t iat_va, uint32_t area_va);

#endif /* CORE_H */
