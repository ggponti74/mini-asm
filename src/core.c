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
// Services:
//   1 exit(D1)                      2 write(D1, A0, D2)       3 read(D1, A0, D2)
//   4 open(A0 = path, D1 = flags, D2 = mode)    -> D0 = fd
//   5 close(D1)                                 -> D0 = 0
//   6 lseek(D1 = fd, D2 = offset, D3 = whence)  -> D0 = new position
// An unknown service number returns -38 (-ENOSYS) in D0.
//
// Files: the path is a NUL-terminated string at A0. The flags are the Linux
// values on every target (core.h: CORE_O_*): access mode in bits 0-1 (0 read,
// 1 write, 2 read/write), CREAT 0x40, EXCL 0x80, TRUNC 0x200, APPEND 0x400.
// mode (the permission bits for a newly created file) is used by the Linux
// targets and ignored on Windows. whence is 0 = start, 1 = current, 2 = end.
// Offsets and positions are 32-bit signed, so files are limited to 2 GB - 1.
// An fd is whatever the target's OS hands out: small integers on Linux, the
// raw Windows HANDLE value on PE. Programs must not assume fds start at 3.
// fds 0, 1 and 2 are stdin, stdout and stderr everywhere.
//
// The code in a dispatcher is position independent (relative jumps and
// INT 0x80 only), so it can be copied anywhere.

// ---------------------------------------------------------------------------
// Linux x86 (-t elf)
//
// Register map on this target: D0=EAX D1=ECX D2=EDX D3=EBX, A0=ESI.
// Linux i386 syscalls take: EAX = number, EBX/ECX/EDX = arguments 1..3, and
// return in EAX, preserving everything else. The 68K convention says every
// register except D0 survives, and several services have to move values into
// EBX/ECX/EDX, so the dispatcher saves all three on entry and restores them
// before returning. The per-service translation (68K -> syscall):
//
//   exit  (1 -> 1)    EBX <- D1 (ECX)
//   write (2 -> 4)    EBX <- D1 (fd), ECX <- A0 (ESI, buf), EDX = D2 (len) as is
//   read  (3 -> 3)    same moves as write
//   open  (4 -> 5)    EBX <- A0 (path); ECX = D1 (flags) and EDX = D2 (mode) as is
//   close (5 -> 6)    EBX <- D1 (fd)
//   lseek (6 -> 19)   needs EBX=fd (D1=ECX), ECX=offset (D2=EDX), EDX=whence
//                     (D3=EBX): two XCHGs rotate the three registers into place
//
// The flag values are passed to the kernel unchanged (i386 Linux uses the
// same O_* numbers as CORE_O_*). On failure the kernel returns -errno in EAX,
// which is exactly the 68K-side convention. The code (the listing below was
// produced by GNU as --32 and objdump) is position independent: relative
// jumps and INT 0x80 only, so it can be copied anywhere.
// ---------------------------------------------------------------------------
static const uint8_t k_elf_dispatcher[] = {
    0x53, //   0  push   ebx
    0x51, //   1  push   ecx
    0x52, //   2  push   edx
    0x83,
    0xF8,
    0x01, //   3  cmp    eax,0x1
    0x74,
    0x20, //   6  je     28 <do_exit>
    0x83,
    0xF8,
    0x02, //   8  cmp    eax,0x2
    0x74,
    0x26, //  11  je     33 <do_write>
    0x83,
    0xF8,
    0x03, //  13  cmp    eax,0x3
    0x74,
    0x2E, //  16  je     40 <do_read>
    0x83,
    0xF8,
    0x04, //  18  cmp    eax,0x4
    0x74,
    0x36, //  21  je     4d <do_open>
    0x83,
    0xF8,
    0x05, //  23  cmp    eax,0x5
    0x74,
    0x3C, //  26  je     58 <do_close>
    0x83,
    0xF8,
    0x06, //  28  cmp    eax,0x6
    0x74,
    0x42, //  31  je     63 <do_lseek>
    0xB8,
    0xDA,
    0xFF,
    0xFF,
    0xFF, //  33  mov    eax,0xffffffda
    0xEB,
    0x46, //  38  jmp    6e <done>
    0x89,
    0xCB, //  40  mov    ebx,ecx
    0xB8,
    0x01,
    0x00,
    0x00,
    0x00, //  42  mov    eax,0x1
    0xCD,
    0x80, //  47  int    0x80
    0xEB,
    0x3B, //  49  jmp    6e <done>
    0x89,
    0xCB, //  51  mov    ebx,ecx
    0x89,
    0xF1, //  53  mov    ecx,esi
    0xB8,
    0x04,
    0x00,
    0x00,
    0x00, //  55  mov    eax,0x4
    0xCD,
    0x80, //  60  int    0x80
    0xEB,
    0x2E, //  62  jmp    6e <done>
    0x89,
    0xCB, //  64  mov    ebx,ecx
    0x89,
    0xF1, //  66  mov    ecx,esi
    0xB8,
    0x03,
    0x00,
    0x00,
    0x00, //  68  mov    eax,0x3
    0xCD,
    0x80, //  73  int    0x80
    0xEB,
    0x21, //  75  jmp    6e <done>
    0x89,
    0xF3, //  77  mov    ebx,esi
    0xB8,
    0x05,
    0x00,
    0x00,
    0x00, //  79  mov    eax,0x5
    0xCD,
    0x80, //  84  int    0x80
    0xEB,
    0x16, //  86  jmp    6e <done>
    0x89,
    0xCB, //  88  mov    ebx,ecx
    0xB8,
    0x06,
    0x00,
    0x00,
    0x00, //  90  mov    eax,0x6
    0xCD,
    0x80, //  95  int    0x80
    0xEB,
    0x0B, //  97  jmp    6e <done>
    0x87,
    0xCB, //  99  xchg   ebx,ecx
    0x87,
    0xD1, // 101  xchg   ecx,edx
    0xB8,
    0x13,
    0x00,
    0x00,
    0x00, // 103  mov    eax,0x13
    0xCD,
    0x80, // 108  int    0x80
    0x5A, // 110  pop    edx
    0x59, // 111  pop    ecx
    0x5B, // 112  pop    ebx
    0x85,
    0xC0, // 113  test   eax,eax
    0xC3, // 115  ret
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
// r0. Numbers: exit 1, read 3, write 4, open 5, close 6, lseek 19. Each
// service loads its arguments from the register block (D1 = +4, D2 = +8,
// D3 = +12, A0 = +32):
//
//   exit  r0 = D1
//   write r0 = D1 (fd), r1 = A0 (buf), r2 = D2 (len)        read: same
//   open  r0 = A0 (path), r1 = D1 (flags), r2 = D2 (mode)
//   close r0 = D1 (fd)
//   lseek r0 = D1 (fd), r1 = D2 (offset), r2 = D3 (whence)
//
// The O_* flag numbers are the same on ARM Linux as on i386, so they are
// passed through unchanged. After every service the result goes back to D0 in
// the register block, N and Z are set from it and C and V are cleared. The
// listing below was assembled with arm-linux-gnueabi-as -march=armv6 and
// checked with objdump.
// ---------------------------------------------------------------------------
#define ARM_WORD(w) (uint8_t)((w) & 0xFF), (uint8_t)(((w) >> 8) & 0xFF), \
                    (uint8_t)(((w) >> 16) & 0xFF), (uint8_t)(((w) >> 24) & 0xFF)

static const uint8_t k_arm_dispatcher[] = {
    ARM_WORD(0xE59C0000u), //   0 ldr r0, [ip]
    ARM_WORD(0xE3500001u), //   1 cmp r0, #1
    ARM_WORD(0x0A00000Bu), //   2 beq 3c <do_exit>
    ARM_WORD(0xE3500002u), //   3 cmp r0, #2
    ARM_WORD(0x0A00000Du), //   4 beq 4c <do_write>
    ARM_WORD(0xE3500003u), //   5 cmp r0, #3
    ARM_WORD(0x0A000011u), //   6 beq 64 <do_read>
    ARM_WORD(0xE3500004u), //   7 cmp r0, #4
    ARM_WORD(0x0A000015u), //   8 beq 7c <do_open>
    ARM_WORD(0xE3500005u), //   9 cmp r0, #5
    ARM_WORD(0x0A000019u), //  10 beq 94 <do_close>
    ARM_WORD(0xE3500006u), //  11 cmp r0, #6
    ARM_WORD(0x0A00001Bu), //  12 beq a4 <do_lseek>
    ARM_WORD(0xE3E00025u), //  13 mvn r0, #37 @ 0x25
    ARM_WORD(0xEA00001Eu), //  14 b b8 <done>
    ARM_WORD(0xE59C0004u), //  15 ldr r0, [ip, #4]
    ARM_WORD(0xE3A07001u), //  16 mov r7, #1
    ARM_WORD(0xEF000000u), //  17 svc 0x00000000
    ARM_WORD(0xEA00001Au), //  18 b b8 <done>
    ARM_WORD(0xE59C0004u), //  19 ldr r0, [ip, #4]
    ARM_WORD(0xE59C1020u), //  20 ldr r1, [ip, #32]
    ARM_WORD(0xE59C2008u), //  21 ldr r2, [ip, #8]
    ARM_WORD(0xE3A07004u), //  22 mov r7, #4
    ARM_WORD(0xEF000000u), //  23 svc 0x00000000
    ARM_WORD(0xEA000014u), //  24 b b8 <done>
    ARM_WORD(0xE59C0004u), //  25 ldr r0, [ip, #4]
    ARM_WORD(0xE59C1020u), //  26 ldr r1, [ip, #32]
    ARM_WORD(0xE59C2008u), //  27 ldr r2, [ip, #8]
    ARM_WORD(0xE3A07003u), //  28 mov r7, #3
    ARM_WORD(0xEF000000u), //  29 svc 0x00000000
    ARM_WORD(0xEA00000Eu), //  30 b b8 <done>
    ARM_WORD(0xE59C0020u), //  31 ldr r0, [ip, #32]
    ARM_WORD(0xE59C1004u), //  32 ldr r1, [ip, #4]
    ARM_WORD(0xE59C2008u), //  33 ldr r2, [ip, #8]
    ARM_WORD(0xE3A07005u), //  34 mov r7, #5
    ARM_WORD(0xEF000000u), //  35 svc 0x00000000
    ARM_WORD(0xEA000008u), //  36 b b8 <done>
    ARM_WORD(0xE59C0004u), //  37 ldr r0, [ip, #4]
    ARM_WORD(0xE3A07006u), //  38 mov r7, #6
    ARM_WORD(0xEF000000u), //  39 svc 0x00000000
    ARM_WORD(0xEA000004u), //  40 b b8 <done>
    ARM_WORD(0xE59C0004u), //  41 ldr r0, [ip, #4]
    ARM_WORD(0xE59C1008u), //  42 ldr r1, [ip, #8]
    ARM_WORD(0xE59C200Cu), //  43 ldr r2, [ip, #12]
    ARM_WORD(0xE3A07013u), //  44 mov r7, #19
    ARM_WORD(0xEF000000u), //  45 svc 0x00000000
    ARM_WORD(0xE58C0000u), //  46 str r0, [ip]
    ARM_WORD(0xE1100000u), //  47 tst r0, r0
    ARM_WORD(0xE10F1000u), //  48 mrs r1, CPSR
    ARM_WORD(0xE3C11203u), //  49 bic r1, r1, #805306368 @ 0x30000000
    ARM_WORD(0xE128F001u), //  50 msr CPSR_f, r1
    ARM_WORD(0xE12FFF1Eu)  //  51 bx lr
};

typedef char core_arm_size_check[(sizeof(k_arm_dispatcher) == CORE_ARM_DISPATCHER_SIZE) ? 1 : -1];

// ---------------------------------------------------------------------------
// Windows x86 (-t x86, PE32)
//
// Windows has no stable syscall interface (the numbers change between
// versions), so the services go through kernel32: ExitProcess, GetStdHandle,
// WriteFile, ReadFile, CreateFileA, CloseHandle, SetFilePointer (and
// GetLastError, to turn failures into -errno). The PE writer puts an import
// block in the image and hands us its address.
//
// Register map is the same as on the elf target: D0=EAX D1=ECX D2=EDX D3=EBX,
// A0=ESI. The Windows calls are stdcall (callee pops the arguments) and
// clobber EAX, ECX and EDX, but preserve EBX, ESI, EDI and EBP. So only D1
// and D2 need saving; the dispatcher does that on entry.
//
// File descriptors: 0 = stdin (read only), 1 = stdout and 2 = stderr (write
// only); the wrong direction returns -9 (-EBADF). Any other fd is the raw
// Windows HANDLE that open returned, so the dispatcher keeps no table. Closing
// fd 0, 1 or 2 does nothing and returns 0, and lseek on them returns -29
// (-ESPIPE). Reading from a closed pipe (ERROR_BROKEN_PIPE) is end of input
// and returns 0.
//
// open translates the Linux-valued flags into CreateFileA arguments:
//   access:       RDONLY -> GENERIC_READ, WRONLY -> GENERIC_WRITE,
//                 RDWR -> both, access mode 3 -> -22 (-EINVAL)
//   APPEND:       GENERIC_WRITE becomes FILE_APPEND_DATA, so every write goes to
//                 the end of the file whatever the position (like O_APPEND);
//                 ignored for a read-only open
//   disposition:  CREAT|EXCL -> CREATE_NEW, CREAT|TRUNC -> CREATE_ALWAYS,
//                 CREAT -> OPEN_ALWAYS, TRUNC alone -> TRUNCATE_EXISTING,
//                 none -> OPEN_EXISTING
//   sharing is FILE_SHARE_READ | FILE_SHARE_WRITE; the mode argument is ignored.
// (TRUNCATE_EXISTING needs write access, so WRONLY|APPEND|TRUNC without CREAT
// fails on Windows with -13; with CREAT it works.)
//
// Errors (GetLastError -> -errno): 2 and 3 -> -2 (ENOENT), 5 and 32 -> -13
// (EACCES), 6 -> -9 (EBADF), 80 and 183 -> -17 (EEXIST), 87 and 131 -> -22
// (EINVAL), 1 -> -29 (ESPIPE), anything else -> -5 (EIO).
//
// The code was assembled with GNU as (as --32, Intel syntax); the IAT operands
// (FF 15 abs32) are placeholders that core_pe_emit_dispatcher() fills:
// ---------------------------------------------------------------------------
static const uint8_t k_pe_dispatcher[] = {
    0x51, 0x52, 0x83, 0xF8, 0x01, 0x74, 0x2F, 0x83, 0xF8, 0x02, 0x74, 0x32,
    0x83, 0xF8, 0x03, 0x74, 0x72, 0x83, 0xF8, 0x04, 0x0F, 0x84, 0xB5, 0x00,
    0x00, 0x00, 0x83, 0xF8, 0x05, 0x0F, 0x84, 0x48, 0x01, 0x00, 0x00, 0x83,
    0xF8, 0x06, 0x0F, 0x84, 0x56, 0x01, 0x00, 0x00, 0xB8, 0xDA, 0xFF, 0xFF,
    0xFF, 0xE9, 0xCC, 0x01, 0x00, 0x00, 0x51, 0xFF, 0x15, 0x00, 0x00, 0xAA,
    0xAA, 0xCC, 0x83, 0xF9, 0x01, 0x74, 0x11, 0x83, 0xF9, 0x02, 0x74, 0x10,
    0x85, 0xC9, 0x0F, 0x84, 0x98, 0x01, 0x00, 0x00, 0x89, 0xC8, 0xEB, 0x0C,
    0x6A, 0xF5, 0xEB, 0x02, 0x6A, 0xF4, 0xFF, 0x15, 0x01, 0x00, 0xAA, 0xAA,
    0x8B, 0x14, 0x24, 0x6A, 0x00, 0x89, 0xE1, 0x6A, 0x00, 0x51, 0x52, 0x56,
    0x50, 0xFF, 0x15, 0x02, 0x00, 0xAA, 0xAA, 0x59, 0x85, 0xC0, 0x0F, 0x84,
    0x1B, 0x01, 0x00, 0x00, 0x89, 0xC8, 0xE9, 0x7F, 0x01, 0x00, 0x00, 0x85,
    0xC9, 0x74, 0x0D, 0x83, 0xF9, 0x02, 0x0F, 0x86, 0x58, 0x01, 0x00, 0x00,
    0x89, 0xC8, 0xEB, 0x08, 0x6A, 0xF6, 0xFF, 0x15, 0x01, 0x00, 0xAA, 0xAA,
    0x8B, 0x14, 0x24, 0x6A, 0x00, 0x89, 0xE1, 0x6A, 0x00, 0x51, 0x52, 0x56,
    0x50, 0xFF, 0x15, 0x03, 0x00, 0xAA, 0xAA, 0x59, 0x85, 0xC0, 0x75, 0x14,
    0xFF, 0x15, 0x04, 0x00, 0xAA, 0xAA, 0x83, 0xF8, 0x6D, 0x0F, 0x84, 0xB8,
    0x00, 0x00, 0x00, 0xE9, 0xD5, 0x00, 0x00, 0x00, 0x89, 0xC8, 0xE9, 0x33,
    0x01, 0x00, 0x00, 0x89, 0xC8, 0x83, 0xE0, 0x03, 0x83, 0xF8, 0x03, 0x0F,
    0x84, 0x19, 0x01, 0x00, 0x00, 0xBA, 0x00, 0x00, 0x00, 0x80, 0x83, 0xF8,
    0x00, 0x74, 0x0F, 0xBA, 0x00, 0x00, 0x00, 0x40, 0x83, 0xF8, 0x01, 0x74,
    0x05, 0xBA, 0x00, 0x00, 0x00, 0xC0, 0xF7, 0xC1, 0x00, 0x04, 0x00, 0x00,
    0x74, 0x11, 0xF7, 0xC2, 0x00, 0x00, 0x00, 0x40, 0x74, 0x09, 0x81, 0xE2,
    0xFF, 0xFF, 0xFF, 0xBF, 0x83, 0xCA, 0x04, 0xB8, 0x03, 0x00, 0x00, 0x00,
    0xF7, 0xC1, 0x40, 0x00, 0x00, 0x00, 0x74, 0x23, 0xB8, 0x04, 0x00, 0x00,
    0x00, 0xF7, 0xC1, 0x80, 0x00, 0x00, 0x00, 0x74, 0x07, 0xB8, 0x01, 0x00,
    0x00, 0x00, 0xEB, 0x1C, 0xF7, 0xC1, 0x00, 0x02, 0x00, 0x00, 0x74, 0x14,
    0xB8, 0x02, 0x00, 0x00, 0x00, 0xEB, 0x0D, 0xF7, 0xC1, 0x00, 0x02, 0x00,
    0x00, 0x74, 0x05, 0xB8, 0x05, 0x00, 0x00, 0x00, 0x6A, 0x00, 0x68, 0x80,
    0x00, 0x00, 0x00, 0x50, 0x6A, 0x00, 0x6A, 0x03, 0x52, 0x56, 0xFF, 0x15,
    0x06, 0x00, 0xAA, 0xAA, 0x83, 0xF8, 0xFF, 0x0F, 0x85, 0x99, 0x00, 0x00,
    0x00, 0xEB, 0x2C, 0x83, 0xF9, 0x02, 0x76, 0x0B, 0x51, 0xFF, 0x15, 0x07,
    0x00, 0xAA, 0xAA, 0x85, 0xC0, 0x74, 0x1C, 0x31, 0xC0, 0xE9, 0x80, 0x00,
    0x00, 0x00, 0x83, 0xF9, 0x02, 0x76, 0x76, 0x53, 0x6A, 0x00, 0x52, 0x51,
    0xFF, 0x15, 0x08, 0x00, 0xAA, 0xAA, 0x83, 0xF8, 0xFF, 0x75, 0x6B, 0xFF,
    0x15, 0x04, 0x00, 0xAA, 0xAA, 0x83, 0xF8, 0x02, 0x74, 0x38, 0x83, 0xF8,
    0x03, 0x74, 0x33, 0x83, 0xF8, 0x05, 0x74, 0x35, 0x83, 0xF8, 0x20, 0x74,
    0x30, 0x83, 0xF8, 0x06, 0x74, 0x32, 0x83, 0xF8, 0x50, 0x74, 0x34, 0x3D,
    0xB7, 0x00, 0x00, 0x00, 0x74, 0x2D, 0x83, 0xF8, 0x57, 0x74, 0x2F, 0x3D,
    0x83, 0x00, 0x00, 0x00, 0x74, 0x28, 0x83, 0xF8, 0x01, 0x74, 0x2A, 0xB8,
    0xFB, 0xFF, 0xFF, 0xFF, 0xEB, 0x28, 0xB8, 0xFE, 0xFF, 0xFF, 0xFF, 0xEB,
    0x21, 0xB8, 0xF3, 0xFF, 0xFF, 0xFF, 0xEB, 0x1A, 0xB8, 0xF7, 0xFF, 0xFF,
    0xFF, 0xEB, 0x13, 0xB8, 0xEF, 0xFF, 0xFF, 0xFF, 0xEB, 0x0C, 0xB8, 0xEA,
    0xFF, 0xFF, 0xFF, 0xEB, 0x05, 0xB8, 0xE3, 0xFF, 0xFF, 0xFF, 0x5A, 0x59,
    0x85, 0xC0, 0xC3, 0xCC};

typedef char core_pe_size_check[(sizeof(k_pe_dispatcher) == CORE_PE_DISPATCHER_SIZE) ? 1 : -1];

// Where the IAT operands are in k_pe_dispatcher, and which import each one is.
enum
{
    PE_IMP_EXITPROCESS,
    PE_IMP_GETSTDHANDLE,
    PE_IMP_WRITEFILE,
    PE_IMP_READFILE,
    PE_IMP_GETLASTERROR,
    PE_IMP_GETCOMMANDLINEA,
    PE_IMP_CREATEFILEA,
    PE_IMP_CLOSEHANDLE,
    PE_IMP_SETFILEPOINTER,
    PE_IMP_COUNT
};

// offset needs 16 bits now: the dispatcher is longer than 255 bytes
static const struct
{
    uint16_t offset;
    uint8_t import;
} k_pe_iat_patches[] = {
    {57, PE_IMP_EXITPROCESS},
    {92, PE_IMP_GETSTDHANDLE},
    {111, PE_IMP_WRITEFILE},
    {152, PE_IMP_GETSTDHANDLE},
    {171, PE_IMP_READFILE},
    {182, PE_IMP_GETLASTERROR},
    {348, PE_IMP_CREATEFILEA},
    {371, PE_IMP_CLOSEHANDLE},
    {398, PE_IMP_SETFILEPOINTER},
    {409, PE_IMP_GETLASTERROR}};

static const char *const k_pe_import_names[PE_IMP_COUNT] = {
    "ExitProcess", "GetStdHandle", "WriteFile", "ReadFile", "GetLastError", "GetCommandLineA",
    "CreateFileA", "CloseHandle", "SetFilePointer"};

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
//  80  IAT (import address table): same values; the loader overwrites them
// 120  hint/name entries (2-byte hint, name, NUL, padded to an even size)
//      then "kernel32.dll", padded so the block is CORE_PE_IMPORT_SIZE bytes
#define PE_ILT_OFFSET 40
typedef char core_pe_iat_check[(CORE_PE_IAT_OFFSET == PE_ILT_OFFSET + 4 * (PE_IMP_COUNT + 1) &&
                                CORE_PE_IAT_SIZE == 4 * (PE_IMP_COUNT + 1))
                                   ? 1
                                   : -1];
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

    put32(out + 0, block_rva + PE_ILT_OFFSET);       // OriginalFirstThunk
    put32(out + 12, block_rva + (uint32_t)pos);      // Name
    put32(out + 16, block_rva + CORE_PE_IAT_OFFSET); // FirstThunk
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
    0x01, 0x00, 0xCC, 0xCC, 0x50, 0xFF, 0x15, 0x01, 0x00, 0xBB, 0xBB, 0xCC};

typedef char core_pe_startup_check[(sizeof(k_pe_startup) == CORE_PE_STARTUP_SIZE) ? 1 : -1];

#define PE_STARTUP_ARGV_OFFSET 32768 // the argv array starts this far into the scratch area

int core_pe_emit_startup(uint8_t *out, size_t cap, uint32_t startup_va, uint32_t user_va,
                         uint32_t iat_va, uint32_t area_va)
{
    if (!out || cap < sizeof(k_pe_startup))
        return -1;
    memcpy(out, k_pe_startup, sizeof(k_pe_startup));
    put32(out + 2, iat_va + 4u * PE_IMP_GETCOMMANDLINEA); // call [GetCommandLineA]
    put32(out + 9, area_va);                              // string area
    put32(out + 14, area_va + PE_STARTUP_ARGV_OFFSET);    // argv array
    put32(out + 228, user_va - (startup_va + 228 + 4));   // call <program>: rel32
    put32(out + 235, iat_va + 4u * PE_IMP_EXITPROCESS);   // call [ExitProcess]
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