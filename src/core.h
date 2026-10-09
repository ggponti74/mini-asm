#ifndef CORE_H
#define CORE_H

#include <stddef.h> // size_t
#include <stdint.h> // uint8_t

// void exit(void);
void write(void);
void read(void);

#define CORE_SVC_EXIT 1
#define CORE_SVC_WRITE 2
#define CORE_SVC_READ 3

#define CORE_ELF_DISPATCHER_SIZE 64  // core.c checks this at compile time
#define CORE_ARM_DISPATCHER_SIZE 120 // likewise (30 ARM instructions)

// Windows PE (x86): the dispatcher calls kernel32 through the import table, so
// it needs the table's address; use these two instead of core_emit_dispatcher().
#define CORE_PE_IMPORT_SIZE 176     // the import block (directory, ILT, IAT, names)
#define CORE_PE_IAT_OFFSET 64       // where the IAT sits inside the import block
#define CORE_PE_DISPATCHER_SIZE 164 // the dispatcher itself

size_t core_dispatcher_size(const char *target); // 0 = unsupported target
int core_emit_dispatcher(const char *target, uint8_t *out, size_t cap);
// bytes written, or -1

// Windows PE: write the CORE_PE_IMPORT_SIZE-byte import block that goes at
// block_rva (an RVA), and the dispatcher, which calls through that block's IAT
// (block_va = image base + block_rva). Each returns the bytes written, or -1.
int core_pe_emit_imports(uint8_t *out, size_t cap, uint32_t block_rva);
int core_pe_emit_dispatcher(uint8_t *out, size_t cap, uint32_t block_va);

#endif /* CORE_H */