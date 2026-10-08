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
// Target table. Keyed by the PlatformTarget name, not the CPU architecture:
// "elf" and "x86" (the Windows PE target) share an instruction set but not
// an operating system, so they can't share a dispatcher.
//
// "arm": not done yet (ARM Linux: svc #0, number in r7, arguments r0-r2).
// "x86" (PE): not done yet. Windows has no stable syscall interface; it needs
//             kernel32 imports (ExitProcess, WriteFile, ReadFile) in the PE.
// ---------------------------------------------------------------------------
typedef struct
{
    const char *target;
    const uint8_t *code;
    size_t size;
} CoreDispatcher;

static const CoreDispatcher k_dispatchers[] = {
    {"elf", k_elf_dispatcher, sizeof(k_elf_dispatcher)},
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