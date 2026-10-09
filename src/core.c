#include <string.h>

#include "core.h"

// core.c: the TRAP #0 service dispatcher, one per target.
//
// A 68K program asks the "OS" for a service with TRAP #0. mini-asm plays the
// OS: the service numbers and register convention are fixed (see core.h) and
// every target implements them with its own native mechanism. Codegen turns
// TRAP #0 into a CALL to a dispatcher that the writer places once in the
// output, right after the entry stub/epilogue; the dispatcher translates the
// 68K convention into the native system call and back.
//
// 68K-side convention (the same on every target):
//     D0 = service number          D1 = fd (or exit status)
//     A0 = buffer address          D2 = length
//     result in D0: >= 0 success (bytes transferred), < 0 failure (-errno)
//     every register except D0 is preserved
//     on return N and Z reflect D0 (like MOVE), so "trap #0 ; bmi error" works
//     (C and V are cleared; X is not touched)
//
// Services: 1 exit(D1), 2 write(D1, A0, D2), 3 read(D1, A0, D2).
// An unknown service number returns -38 (-ENOSYS) in D0.
//
// The code in a dispatcher is position independent (relative jumps and
// INT 0x80 only), so it can be copied anywhere.

// ---------------------------------------------------------------------------
// Linux x86 (-t elf)
//
// Register map on this target: D0=EAX D1=ECX D2=EDX D3=EBX, A0=ESI.
// Linux i386 syscalls take: EAX = number, EBX/ECX/EDX = arguments 1..3, and
// return in EAX, preserving everything else. So write(fd=D1, buf=A0, len=D2)
// needs EBX <- ECX (D1), ECX <- ESI (A0), EDX = D2 as is. That clobbers EBX
// (D3) and ECX (D1), which the 68K convention says survive, so the
// dispatcher saves them first and restores them before returning.
//
//     0 push ebx
//     1 push ecx
//     2 cmp  eax,1          ; exit?
//     5 je   do_exit  (24)
//     7 cmp  eax,2          ; write?
//    10 je   do_write (35)
//    12 cmp  eax,3          ; read?
//    15 je   do_read  (48)
//    17 mov  eax,-38        ; unknown service: -ENOSYS
//    22 jmp  done     (59)
//    24 do_exit:  mov ebx,ecx ; mov eax,1 ; int 0x80 ; jmp done
//    35 do_write: mov ebx,ecx ; mov ecx,esi ; mov eax,4 ; int 0x80 ; jmp done
//    48 do_read:  mov ebx,ecx ; mov ecx,esi ; mov eax,3 ; int 0x80
//    59 done: pop ecx ; pop ebx
//    61 test eax,eax        ; N/Z from D0, C and V cleared (as MOVE does)
//    63 ret
//
// (sys_exit = 1, sys_read = 3, sys_write = 4 on i386 Linux.)
// ---------------------------------------------------------------------------
static const uint8_t k_elf_dispatcher[] = {
    0x53,                         // push ebx
    0x51,                         // push ecx
    0x83, 0xF8, CORE_SVC_EXIT,    // cmp eax, EXIT
    0x74, 0x11,                   // je  do_exit   (+17)
    0x83, 0xF8, CORE_SVC_WRITE,   // cmp eax, WRITE
    0x74, 0x17,                   // je  do_write  (+23)
    0x83, 0xF8, CORE_SVC_READ,    // cmp eax, READ
    0x74, 0x1F,                   // je  do_read   (+31)
    0xB8, 0xDA, 0xFF, 0xFF, 0xFF, // mov eax, -38  (-ENOSYS)
    0xEB, 0x23,                   // jmp done      (+35)
    // do_exit:
    0x89, 0xCB,                   // mov ebx, ecx  (status = D1)
    0xB8, 0x01, 0x00, 0x00, 0x00, // mov eax, 1    (sys_exit)
    0xCD, 0x80,                   // int 0x80
    0xEB, 0x18,                   // jmp done      (+24)
    // do_write:
    0x89, 0xCB,                   // mov ebx, ecx  (fd  = D1)
    0x89, 0xF1,                   // mov ecx, esi  (buf = A0)
    0xB8, 0x04, 0x00, 0x00, 0x00, // mov eax, 4    (sys_write)
    0xCD, 0x80,                   // int 0x80
    0xEB, 0x0B,                   // jmp done      (+11)
    // do_read:
    0x89, 0xCB,                   // mov ebx, ecx  (fd  = D1)
    0x89, 0xF1,                   // mov ecx, esi  (buf = A0)
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3    (sys_read)
    0xCD, 0x80,                   // int 0x80
    // done:
    0x59,       // pop ecx
    0x5B,       // pop ebx
    0x85, 0xC0, // test eax, eax
    0xC3        // ret
};

// Compile-time check that the table above matches the size core.h promises
// (platform.c's code_tail and the ELF writer depend on it).
typedef char core_elf_size_check[(sizeof(k_elf_dispatcher) == CORE_ELF_DISPATCHER_SIZE) ? 1 : -1];

// ---------------------------------------------------------------------------
// Linux ARM (-t arm), ARM mode, EABI
//
// On this target the 68K registers do not live in ARM registers: they sit in
// the register block in memory, D0-D7 at +0..+28 and A0-A7 at +32..+60, as
// plain 32-bit values. Between two 68K instructions nothing is live in r0-r12,
// so the dispatcher may use them freely. What it needs is the address of the
// register block, which the TRAP call site passes in r12 (see emit_trap in
// codegen.c), so these bytes contain no absolute address and are position
// independent. The call site also saves lr around the BL, exactly as BSR/JSR
// do, so the dispatcher just returns with BX LR.
//
// Linux ARM EABI syscalls: number in r7, arguments in r0-r2, SVC #0, result in
// r0 (sys_exit = 1, sys_read = 3, sys_write = 4). The words below were
// assembled with GNU as (arm-linux-gnueabi-as -march=armv6) from:
//
//     ldr  r0, [r12]            ; D0 = service number
//     cmp  r0, #EXIT   ; beq do_exit
//     cmp  r0, #WRITE  ; beq do_write
//     cmp  r0, #READ   ; beq do_read
//     mvn  r0, #37              ; unknown service: -38 (-ENOSYS)
//     b    done
//   do_exit:  ldr r0,[r12,#4] ; mov r7,#1 ; svc #0 ; b done        (status = D1)
//   do_write: ldr r0,[r12,#4] ; ldr r1,[r12,#32] ; ldr r2,[r12,#8]
//             mov r7,#4 ; svc #0 ; b done      (fd = D1, buf = A0, len = D2)
//   do_read:  ldr r0,[r12,#4] ; ldr r1,[r12,#32] ; ldr r2,[r12,#8]
//             mov r7,#3 ; svc #0
//   done:     str r0,[r12]       ; D0 = result
//             tst r0,r0          ; N and Z from D0 ...
//             mrs r1,cpsr ; bic r1,r1,#0x30000000 ; msr cpsr_f,r1   ; ... C and V cleared
//             bx  lr
// ---------------------------------------------------------------------------
#define ARM_WORD(w) (uint8_t)((w) & 0xFF), (uint8_t)(((w) >> 8) & 0xFF), \
                    (uint8_t)(((w) >> 16) & 0xFF), (uint8_t)(((w) >> 24) & 0xFF)

static const uint8_t k_arm_dispatcher[] = {
    ARM_WORD(0xE59C0000u),                     //  0 ldr  r0, [r12]
    ARM_WORD(0xE3500000u | CORE_SVC_EXIT),     //  1 cmp  r0, #EXIT
    ARM_WORD(0x0A000005u),                     //  2 beq  do_exit  (24)
    ARM_WORD(0xE3500000u | CORE_SVC_WRITE),    //  3 cmp  r0, #WRITE
    ARM_WORD(0x0A000007u),                     //  4 beq  do_write (34)
    ARM_WORD(0xE3500000u | CORE_SVC_READ),     //  5 cmp  r0, #READ
    ARM_WORD(0x0A00000Bu),                     //  6 beq  do_read  (4C)
    ARM_WORD(0xE3E00025u),                     //  7 mvn  r0, #37  (-ENOSYS)
    ARM_WORD(0xEA00000Eu),                     //  8 b    done     (60)
    // do_exit:
    ARM_WORD(0xE59C0004u),                     //  9 ldr  r0, [r12, #4]   status = D1
    ARM_WORD(0xE3A07001u),                     // 10 mov  r7, #1          sys_exit
    ARM_WORD(0xEF000000u),                     // 11 svc  #0
    ARM_WORD(0xEA00000Au),                     // 12 b    done
    // do_write:
    ARM_WORD(0xE59C0004u),                     // 13 ldr  r0, [r12, #4]   fd  = D1
    ARM_WORD(0xE59C1020u),                     // 14 ldr  r1, [r12, #32]  buf = A0
    ARM_WORD(0xE59C2008u),                     // 15 ldr  r2, [r12, #8]   len = D2
    ARM_WORD(0xE3A07004u),                     // 16 mov  r7, #4          sys_write
    ARM_WORD(0xEF000000u),                     // 17 svc  #0
    ARM_WORD(0xEA000004u),                     // 18 b    done
    // do_read:
    ARM_WORD(0xE59C0004u),                     // 19 ldr  r0, [r12, #4]   fd  = D1
    ARM_WORD(0xE59C1020u),                     // 20 ldr  r1, [r12, #32]  buf = A0
    ARM_WORD(0xE59C2008u),                     // 21 ldr  r2, [r12, #8]   len = D2
    ARM_WORD(0xE3A07003u),                     // 22 mov  r7, #3          sys_read
    ARM_WORD(0xEF000000u),                     // 23 svc  #0
    // done:
    ARM_WORD(0xE58C0000u),                     // 24 str  r0, [r12]       D0 = result
    ARM_WORD(0xE1100000u),                     // 25 tst  r0, r0          N, Z from D0
    ARM_WORD(0xE10F1000u),                     // 26 mrs  r1, cpsr
    ARM_WORD(0xE3C11203u),                     // 27 bic  r1, r1, #0x30000000   clear C, V
    ARM_WORD(0xE128F001u),                     // 28 msr  cpsr_f, r1
    ARM_WORD(0xE12FFF1Eu)                      // 29 bx   lr
};

typedef char core_arm_size_check[(sizeof(k_arm_dispatcher) == CORE_ARM_DISPATCHER_SIZE) ? 1 : -1];

// ---------------------------------------------------------------------------
// Windows x86 (-t x86, PE32)
//
// Windows has no stable syscall interface (the numbers change between
// versions), so the services go through kernel32: ExitProcess, GetStdHandle,
// WriteFile, ReadFile (and GetLastError, to tell end of input from an error).
// The PE writer puts an import block in the image and hands us its address.
//
// Register map is the same as on the elf target: D0=EAX D1=ECX D2=EDX D3=EBX,
// A0=ESI. The Windows calls are stdcall (callee pops the arguments) and
// clobber EAX, ECX and EDX, but preserve EBX, ESI, EDI and EBP. So only D1
// and D2 need saving; the dispatcher does that on entry.
//
// File descriptors: 0 = stdin (read only), 1 = stdout and 2 = stderr (write
// only). Any other fd, or the wrong direction, returns -9 (-EBADF), like the
// other targets do for a bad fd. Other failures return -5 (-EIO). Reading
// from a closed pipe (ERROR_BROKEN_PIPE) is end of input and returns 0.
// The code was assembled with GNU as (as --32, Intel syntax); the 5 IAT
// operands (FF 15 abs32) are placeholders that core_pe_emit_dispatcher() fills:
// ---------------------------------------------------------------------------
static const uint8_t k_pe_dispatcher[] = {
    0x51, 0x52, 0x83, 0xF8, 0x01, 0x74, 0x14, 0x83, 0xF8, 0x02, 0x74, 0x18,
    0x83, 0xF8, 0x03, 0x74, 0x47, 0xB8, 0xDA, 0xFF, 0xFF, 0xFF, 0xE9, 0x82,
    0x00, 0x00, 0x00, 0x51, 0xFF, 0x15, 0x00, 0x00, 0xAA, 0xAA, 0xEB, 0x79,
    0x83, 0xF9, 0x01, 0x74, 0x07, 0x83, 0xF9, 0x02, 0x74, 0x06, 0xEB, 0x68,
    0x6A, 0xF5, 0xEB, 0x02, 0x6A, 0xF4, 0xFF, 0x15, 0x01, 0x00, 0xAA, 0xAA,
    0x8B, 0x14, 0x24, 0x6A, 0x00, 0x89, 0xE1, 0x6A, 0x00, 0x51, 0x52, 0x56,
    0x50, 0xFF, 0x15, 0x02, 0x00, 0xAA, 0xAA, 0x59, 0x85, 0xC0, 0x74, 0x3D,
    0x89, 0xC8, 0xEB, 0x45, 0x85, 0xC9, 0x75, 0x3C, 0x6A, 0xF6, 0xFF, 0x15,
    0x01, 0x00, 0xAA, 0xAA, 0x8B, 0x14, 0x24, 0x6A, 0x00, 0x89, 0xE1, 0x6A,
    0x00, 0x51, 0x52, 0x56, 0x50, 0xFF, 0x15, 0x03, 0x00, 0xAA, 0xAA, 0x59,
    0x85, 0xC0, 0x75, 0x0D, 0xFF, 0x15, 0x04, 0x00, 0xAA, 0xAA, 0x83, 0xF8,
    0x6D, 0x74, 0x06, 0xEB, 0x08, 0x89, 0xC8, 0xEB, 0x10, 0x31, 0xC0, 0xEB,
    0x0C, 0xB8, 0xFB, 0xFF, 0xFF, 0xFF, 0xEB, 0x05, 0xB8, 0xF7, 0xFF, 0xFF,
    0xFF, 0x5A, 0x59, 0x85, 0xC0, 0xC3, 0xCC, 0xCC
};

typedef char core_pe_size_check[(sizeof(k_pe_dispatcher) == CORE_PE_DISPATCHER_SIZE) ? 1 : -1];

// Where the IAT operands are in k_pe_dispatcher, and which import each one is.
enum { PE_IMP_EXITPROCESS, PE_IMP_GETSTDHANDLE, PE_IMP_WRITEFILE, PE_IMP_READFILE,
       PE_IMP_GETLASTERROR, PE_IMP_GETCOMMANDLINEA, PE_IMP_COUNT };

static const struct
{
    uint8_t offset;
    uint8_t import;
} k_pe_iat_patches[] = {
    {30, 0},
    {56, 1},
    {75, 2},
    {96, 1},
    {115, 3},
    {126, 4}
};

static const char *const k_pe_import_names[PE_IMP_COUNT] = {
    "ExitProcess", "GetStdHandle", "WriteFile", "ReadFile", "GetLastError", "GetCommandLineA"
};

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

// Import block layout (offsets from the start of the block):
//   0  import directory: one descriptor for kernel32.dll + a null one (2 x 20)
//  40  ILT (import lookup table): PE_IMP_COUNT hint/name RVAs + 0
//  68  IAT (import address table): same values; the loader overwrites them
//  96  hint/name entries (2-byte hint, name, NUL, padded to an even size)
//      then "kernel32.dll", padded so the block is CORE_PE_IMPORT_SIZE bytes
#define PE_ILT_OFFSET 40
typedef char core_pe_iat_check[(CORE_PE_IAT_OFFSET == PE_ILT_OFFSET + 4 * (PE_IMP_COUNT + 1) &&
                                CORE_PE_IAT_SIZE == 4 * (PE_IMP_COUNT + 1)) ? 1 : -1];
int core_pe_emit_imports(uint8_t *out, size_t cap, uint32_t block_rva)
{
    if (!out || cap < CORE_PE_IMPORT_SIZE)
        return -1;
    memset(out, 0, CORE_PE_IMPORT_SIZE);

    size_t pos = PE_ILT_OFFSET + 4 * (PE_IMP_COUNT + 1) + 4 * (PE_IMP_COUNT + 1);
    for (int i = 0; i < PE_IMP_COUNT; i++)
    {
        size_t len = strlen(k_pe_import_names[i]);
        put32(out + PE_ILT_OFFSET + 4 * i, block_rva + (uint32_t)pos);
        put32(out + CORE_PE_IAT_OFFSET + 4 * i, block_rva + (uint32_t)pos);
        memcpy(out + pos + 2, k_pe_import_names[i], len); // the 2-byte hint stays 0
        pos += (2 + len + 1 + 1) & ~(size_t)1;            // hint + name + NUL, even
    }
    if (pos + sizeof("kernel32.dll") > CORE_PE_IMPORT_SIZE)
        return -1;
    memcpy(out + pos, "kernel32.dll", sizeof("kernel32.dll"));

    put32(out + 0, block_rva + PE_ILT_OFFSET);          // OriginalFirstThunk
    put32(out + 12, block_rva + (uint32_t)pos);         // Name
    put32(out + 16, block_rva + CORE_PE_IAT_OFFSET);    // FirstThunk
    return CORE_PE_IMPORT_SIZE;
}

int core_pe_emit_dispatcher(uint8_t *out, size_t cap, uint32_t block_va)
{
    if (!out || cap < sizeof(k_pe_dispatcher))
        return -1;
    memcpy(out, k_pe_dispatcher, sizeof(k_pe_dispatcher));
    for (size_t i = 0; i < sizeof(k_pe_iat_patches) / sizeof(k_pe_iat_patches[0]); i++)
        put32(out + k_pe_iat_patches[i].offset,
              block_va + CORE_PE_IAT_OFFSET + 4u * k_pe_iat_patches[i].import);
    return (int)sizeof(k_pe_dispatcher);
}

// ---------------------------------------------------------------------------
// Windows x86 entry stub: argc/argv
//
// ELF and ARM start programs with D0 = argc (counting the command itself) and
// A0 = argv, an array of big-endian pointers to NUL-terminated strings that
// ends with a NULL pointer. Windows only hands a process one command-line
// string, so this stub (the PE entry point) does the splitting, with the rules
// the MSVC runtime documents for C/C++ programs:
//   - arguments are separated by spaces and tabs;
//   - a "quoted string" is part of one argument, whatever it contains;
//   - 2n backslashes before a quote give n backslashes, and the quote then
//     opens or closes a group; 2n+1 backslashes give n backslashes and a
//     literal quote; backslashes not followed by a quote are literal;
//   - argv[0] is special: with a leading quote it runs to the next quote, with
//     no backslash handling; otherwise it runs to the first space or tab.
// Not implemented: the newer runtimes' rule that "" inside a quoted group means
// a literal quote; here it just closes and reopens the group.
//
// The strings are copied into a scratch area (the parsed text is never longer
// than the command line plus its NUL, at most 32768 bytes), the pointers go
// to the argv array right after it, byte-swapped for the 68K view of memory.
// Registers: ESI = source, EDI = string output, EBX = argv output, EBP = argv
// base, EDX = inside-quotes flag. When the program returns, its D0 (EAX) is
// the exit code. Assembled with GNU as (as --32, Intel syntax); the five
// operands below are placeholders that core_pe_emit_startup() fills.
// ---------------------------------------------------------------------------
static const uint8_t k_pe_startup[] = {
    0xFF, 0x15, 0x00, 0x00, 0xBB, 0xBB, 0x89, 0xC6, 0xBF, 0x02, 0x00, 0xBB,
    0xBB, 0xBD, 0x03, 0x00, 0xBB, 0xBB, 0x89, 0xEB, 0x89, 0xF8, 0x0F, 0xC8,
    0x89, 0x03, 0x83, 0xC3, 0x04, 0x80, 0x3E, 0x22, 0x75, 0x11, 0x46, 0x8A,
    0x06, 0x84, 0xC0, 0x74, 0x1E, 0x46, 0x3C, 0x22, 0x74, 0x19, 0x88, 0x07,
    0x47, 0xEB, 0xF0, 0x8A, 0x06, 0x84, 0xC0, 0x74, 0x0E, 0x3C, 0x20, 0x74,
    0x0A, 0x3C, 0x09, 0x74, 0x06, 0x46, 0x88, 0x07, 0x47, 0xEB, 0xEC, 0xC6,
    0x07, 0x00, 0x47, 0x8A, 0x06, 0x3C, 0x20, 0x74, 0x04, 0x3C, 0x09, 0x75,
    0x03, 0x46, 0xEB, 0xF3, 0x84, 0xC0, 0x74, 0x78, 0x89, 0xF8, 0x0F, 0xC8,
    0x89, 0x03, 0x83, 0xC3, 0x04, 0x31, 0xD2, 0x8A, 0x06, 0x84, 0xC0, 0x74,
    0x5E, 0x3C, 0x5C, 0x74, 0x1E, 0x3C, 0x22, 0x74, 0x14, 0x3C, 0x20, 0x74,
    0x0A, 0x3C, 0x09, 0x74, 0x06, 0x88, 0x07, 0x47, 0x46, 0xEB, 0xE4, 0x85,
    0xD2, 0x75, 0xF6, 0xEB, 0x42, 0x83, 0xF2, 0x01, 0x46, 0xEB, 0xD8, 0x31,
    0xC9, 0x80, 0x3E, 0x5C, 0x75, 0x04, 0x41, 0x46, 0xEB, 0xF7, 0x80, 0x3E,
    0x22, 0x74, 0x0B, 0x85, 0xC9, 0x74, 0xC4, 0xC6, 0x07, 0x5C, 0x47, 0x49,
    0xEB, 0xF5, 0x89, 0xC8, 0xD1, 0xE8, 0x85, 0xC0, 0x74, 0x07, 0xC6, 0x07,
    0x5C, 0x47, 0x48, 0xEB, 0xF5, 0xF6, 0xC1, 0x01, 0x74, 0x07, 0xC6, 0x07,
    0x22, 0x47, 0x46, 0xEB, 0xA2, 0x83, 0xF2, 0x01, 0x46, 0xEB, 0x9C, 0xC6,
    0x07, 0x00, 0x47, 0xE9, 0x77, 0xFF, 0xFF, 0xFF, 0xC7, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x89, 0xD8, 0x29, 0xE8, 0xC1, 0xE8, 0x02, 0x89, 0xEE, 0xE8,
    0x01, 0x00, 0xCC, 0xCC, 0x50, 0xFF, 0x15, 0x01, 0x00, 0xBB, 0xBB, 0xCC
};

typedef char core_pe_startup_check[(sizeof(k_pe_startup) == CORE_PE_STARTUP_SIZE) ? 1 : -1];

#define PE_STARTUP_ARGV_OFFSET 32768 // the argv array starts this far into the scratch area

int core_pe_emit_startup(uint8_t *out, size_t cap, uint32_t startup_va, uint32_t user_va,
                         uint32_t iat_va, uint32_t area_va)
{
    if (!out || cap < sizeof(k_pe_startup))
        return -1;
    memcpy(out, k_pe_startup, sizeof(k_pe_startup));
    put32(out + 2, iat_va + 4u * PE_IMP_GETCOMMANDLINEA);   // call [GetCommandLineA]
    put32(out + 9, area_va);                                // string area
    put32(out + 14, area_va + PE_STARTUP_ARGV_OFFSET);      // argv array
    put32(out + 228, user_va - (startup_va + 228 + 4));     // call <program>: rel32
    put32(out + 235, iat_va + 4u * PE_IMP_EXITPROCESS);     // call [ExitProcess]
    return (int)sizeof(k_pe_startup);
}

// ---------------------------------------------------------------------------
// Target table. Keyed by the PlatformTarget name, not the CPU architecture:
// "elf" and "x86" (the Windows PE target) share an instruction set but not
// an operating system, so they can't share a dispatcher.
//
// "x86" (PE): not in this table, because its dispatcher needs the import
// table address; see core_pe_emit_dispatcher() above.
// ---------------------------------------------------------------------------
typedef struct
{
    const char *target;
    const uint8_t *code;
    size_t size;
} CoreDispatcher;

static const CoreDispatcher k_dispatchers[] = {
    {"elf", k_elf_dispatcher, sizeof(k_elf_dispatcher)},
    {"arm", k_arm_dispatcher, sizeof(k_arm_dispatcher)},
};
#define NUM_DISPATCHERS (sizeof(k_dispatchers) / sizeof(k_dispatchers[0]))

static const CoreDispatcher *find_dispatcher(const char *target)
{
    if (!target)
        return NULL;
    for (size_t i = 0; i < NUM_DISPATCHERS; i++)
        if (strcmp(k_dispatchers[i].target, target) == 0)
            return &k_dispatchers[i];
    return NULL;
}

size_t core_dispatcher_size(const char *target)
{
    const CoreDispatcher *d = find_dispatcher(target);
    return d ? d->size : 0;
}

int core_emit_dispatcher(const char *target, uint8_t *out, size_t cap)
{
    const CoreDispatcher *d = find_dispatcher(target);
    if (!d || !out || cap < d->size)
        return -1;
    memcpy(out, d->code, d->size);
    return (int)d->size;
}
