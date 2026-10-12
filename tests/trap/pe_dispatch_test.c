// pe_dispatcher_test.c - tests the Windows (PE) TRAP #0 dispatcher from core.c
// on Linux, without Windows. The dispatcher bytes are the real ones; the
// kernel32 functions it calls through the IAT are fakes (below) that run on
// top of the Linux file API and record how they were called. Needs a 32-bit
// gcc (gcc -m32); tests/run.sh skips this test when there is none:
//
//   gcc -m32 -Wall -I src -o pe_test tests/trap/pe_dispatcher_test.c src/core.c
//   cd /tmp && /path/to/pe_test        (creates t-pe.tmp in the current directory)
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "core.h"

#define HBASE 0x100 // fake HANDLE = Linux fd + HBASE, so a handle never equals its fd

// ---- fake kernel32 (stdcall) -------------------------------------------
static uint32_t g_err;
static int g_createfile_calls, g_closehandle_calls;
static uint32_t g_access, g_share, g_disp, g_attrs;

#define STD __attribute__((stdcall))

static STD void fake_ExitProcess(uint32_t code) { _exit((int)code); }
static STD uint32_t fake_GetStdHandle(int32_t n)
{
    return HBASE + (n == -10 ? 0 : n == -11 ? 1
                                            : 2);
}
static STD uint32_t fake_GetLastError(void) { return g_err; }
static uint32_t errno_to_win(int e)
{
    switch (e)
    {
    case ENOENT:
        return 2;
    case EACCES:
        return 5;
    case EBADF:
        return 6;
    case EEXIST:
        return 80;
    default:
        return 1234;
    }
}
static STD int fake_WriteFile(uint32_t h, const void *buf, uint32_t len, uint32_t *done, void *ov)
{
    (void)ov;
    if (h == 0x7778)
    {
        g_err = 1234;
        return 0;
    }
    ssize_t n = write((int)(h - HBASE), buf, len);
    if (n < 0)
    {
        g_err = errno_to_win(errno);
        return 0;
    }
    *done = (uint32_t)n;
    return 1;
}
static STD int fake_ReadFile(uint32_t h, void *buf, uint32_t len, uint32_t *done, void *ov)
{
    (void)ov;
    if (h == 0x7777)
    {
        g_err = 109;
        return 0;
    } // ERROR_BROKEN_PIPE
    if (h == 0x7778)
    {
        g_err = 1234;
        return 0;
    } // some other failure
    ssize_t n = read((int)(h - HBASE), buf, len);
    if (n < 0)
    {
        g_err = errno_to_win(errno);
        return 0;
    }
    *done = (uint32_t)n;
    return 1;
}
static STD uint32_t fake_CreateFileA(const char *path, uint32_t access, uint32_t share, void *sec,
                                     uint32_t disp, uint32_t attrs, void *tmpl)
{
    (void)sec;
    (void)tmpl;
    g_createfile_calls++;
    g_access = access;
    g_share = share;
    g_disp = disp;
    g_attrs = attrs;
    int fl = 0;
    if (access & 0x80000000u)
        fl = (access & 0x40000000u) ? O_RDWR : O_RDONLY;
    else if (access & 0x40000000u)
        fl = O_WRONLY;
    else if (access & 4)
        fl = O_WRONLY | O_APPEND; // FILE_APPEND_DATA
    switch (disp)
    {
    case 1:
        fl |= O_CREAT | O_EXCL;
        break;
    case 2:
        fl |= O_CREAT | O_TRUNC;
        break;
    case 3:
        break;
    case 4:
        fl |= O_CREAT;
        break;
    case 5:
        fl |= O_TRUNC;
        break;
    }
    int fd = open(path, fl, 0644);
    if (fd < 0)
    {
        g_err = errno_to_win(errno);
        return 0xFFFFFFFFu;
    }
    return HBASE + (uint32_t)fd;
}
static STD int fake_CloseHandle(uint32_t h)
{
    g_closehandle_calls++;
    if (close((int)(h - HBASE)) < 0)
    {
        g_err = 6;
        return 0;
    }
    return 1;
}
static STD uint32_t fake_SetFilePointer(uint32_t h, int32_t lo, int32_t *hi, uint32_t method)
{
    (void)hi;
    if (method > 2)
    {
        g_err = 87;
        return 0xFFFFFFFFu;
    }
    off_t r = lseek((int)(h - HBASE), lo, (int)method);
    if (r < 0)
    {
        g_err = (errno == EINVAL) ? 131 : (errno == ESPIPE ? 1 : 6);
        return 0xFFFFFFFFu;
    }
    return (uint32_t)r;
}

// ---- calling the dispatcher: set D0-D3/A0, call, capture the registers ----
// out: ecx edx ebx esi eflags
extern int call_disp(void *fn, uint32_t svc, uint32_t d1, uint32_t d2, uint32_t d3, const void *a0,
                     uint32_t out[5]);
__asm__(".text\n.globl call_disp\ncall_disp:\n"
        " push %ebp\n push %ebx\n push %esi\n push %edi\n"
        " mov 20(%esp),%edi\n mov 24(%esp),%eax\n mov 28(%esp),%ecx\n mov 32(%esp),%edx\n"
        " mov 36(%esp),%ebx\n mov 40(%esp),%esi\n"
        " call *%edi\n"
        " pushf\n pop %ebp\n"
        " mov 44(%esp),%edi\n"
        " mov %ecx,0(%edi)\n mov %edx,4(%edi)\n mov %ebx,8(%edi)\n mov %esi,12(%edi)\n"
        " mov %ebp,16(%edi)\n"
        " pop %edi\n pop %esi\n pop %ebx\n pop %ebp\n ret\n");

static void *g_disp_code;
static int g_fail, g_checks;
static uint32_t g_out[5];

// TRAP: one service call; also checks that every register but D0 survived and
// that N and Z follow D0 with C and V cleared.
static int TRAP(uint32_t svc, uint32_t d1, uint32_t d2, uint32_t d3, const void *a0)
{
    int r = call_disp(g_disp_code, svc, d1, d2, d3, a0, g_out);
    int bad = g_out[0] != d1 || g_out[1] != d2 || g_out[2] != d3 || g_out[3] != (uint32_t)(uintptr_t)a0;
    uint32_t fl = g_out[4];
    int sf = !!(fl & 0x80), zf = !!(fl & 0x40), cf = fl & 1, of = !!(fl & 0x800);
    if (bad || sf != (r < 0) || zf != (r == 0) || cf || of)
    {
        printf("FAIL  service %u: registers/flags wrong (ecx %x edx %x ebx %x esi %x flags %x, result %d)\n",
               svc, g_out[0], g_out[1], g_out[2], g_out[3], fl, r);
        g_fail++;
    }
    return r;
}

#define EXPECT(what, got, want)                                    \
    do                                                             \
    {                                                              \
        g_checks++;                                                \
        long g_ = (long)(got), w_ = (long)(want);                  \
        if (g_ != w_)                                              \
        {                                                          \
            printf("FAIL  %s: got %ld, want %ld\n", what, g_, w_); \
            g_fail++;                                              \
        }                                                          \
    } while (0)

static int T_open(const char *p, uint32_t flags) { return TRAP(CORE_SVC_OPEN, flags, 420, 77, p); }
static int T_close(int fd) { return TRAP(CORE_SVC_CLOSE, (uint32_t)fd, 5, 6, "x"); }
static int T_seek(int fd, int off, int whence) { return TRAP(CORE_SVC_LSEEK, (uint32_t)fd, (uint32_t)off, (uint32_t)whence, "y"); }
static int T_read(int fd, void *b, int n) { return TRAP(CORE_SVC_READ, (uint32_t)fd, (uint32_t)n, 9, b); }
static int T_write(int fd, const void *b, int n) { return TRAP(CORE_SVC_WRITE, (uint32_t)fd, (uint32_t)n, 9, b); }

int main(void)
{
    // map the dispatcher with its IAT operands pointing at our fakes
    static uint32_t iat[CORE_PE_IAT_SIZE / 4];
    iat[0] = (uint32_t)(uintptr_t)fake_ExitProcess;
    iat[1] = (uint32_t)(uintptr_t)fake_GetStdHandle;
    iat[2] = (uint32_t)(uintptr_t)fake_WriteFile;
    iat[3] = (uint32_t)(uintptr_t)fake_ReadFile;
    iat[4] = (uint32_t)(uintptr_t)fake_GetLastError;
    iat[6] = (uint32_t)(uintptr_t)fake_CreateFileA;
    iat[7] = (uint32_t)(uintptr_t)fake_CloseHandle;
    iat[8] = (uint32_t)(uintptr_t)fake_SetFilePointer;
    uint8_t code[CORE_PE_DISPATCHER_SIZE];
    uint32_t block_va = (uint32_t)(uintptr_t)iat - CORE_PE_IAT_OFFSET;
    if (core_pe_emit_dispatcher(code, sizeof code, block_va) != CORE_PE_DISPATCHER_SIZE)
        return 99;
    g_disp_code = mmap(0, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (g_disp_code == MAP_FAILED)
        return 98;
    memcpy(g_disp_code, code, sizeof code);

    // the import block must hold exactly the 9 names the dispatcher uses
    uint8_t imp[CORE_PE_IMPORT_SIZE];
    EXPECT("import block size", core_pe_emit_imports(imp, sizeof imp, 0x3000), CORE_PE_IMPORT_SIZE);
    static const char *const names[] = {"ExitProcess", "GetStdHandle", "WriteFile", "ReadFile",
                                        "GetLastError", "GetCommandLineA", "CreateFileA", "CloseHandle", "SetFilePointer", "kernel32.dll"};
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++)
    {
        int found = 0;
        for (size_t o = 0; o + strlen(names[i]) < sizeof imp; o++)
            if (memcmp(imp + o, names[i], strlen(names[i]) + 1) == 0)
                found = 1;
        EXPECT(names[i], found, 1);
    }

    unlink("t-pe.tmp");
    char buf[32];

    // ---- flags -> CreateFileA arguments --------------------------------
    static const struct
    {
        uint32_t flags, access, disp;
    } map[] = {
        {0x000, 0x80000000u, 3},
        {0x001, 0x40000000u, 3},
        {0x002, 0xC0000000u, 3},
        {0x040, 0x80000000u, 4},
        {0x041, 0x40000000u, 4},
        {0x042, 0xC0000000u, 4},
        {0x241, 0x40000000u, 2},
        {0x0C1, 0x40000000u, 1},
        {0x2C1, 0x40000000u, 1},
        {0x201, 0x40000000u, 5},
        {0x401, 0x00000004u, 3},
        {0x402, 0x80000004u, 3},
        {0x400, 0x80000000u, 3},
        {0x441, 0x00000004u, 4},
        {0x042 | 0x200, 0xC0000000u, 2},
    };
    for (unsigned i = 0; i < sizeof map / sizeof map[0]; i++)
    {
        char what[64];
        // start clean: OPEN_EXISTING and TRUNCATE_EXISTING need a file, CREATE_NEW must not find one
        unlink("t-pe.tmp");
        if (map[i].disp == 5 || map[i].disp == 3)
        {
            int f = open("t-pe.tmp", O_CREAT | O_WRONLY, 0644);
            close(f);
        }
        snprintf(what, sizeof what, "flags %#x: open succeeds", map[i].flags);
        int fd = T_open("t-pe.tmp", map[i].flags);
        EXPECT(what, fd >= HBASE, 1);
        snprintf(what, sizeof what, "flags %#x: access", map[i].flags);
        EXPECT(what, g_access, map[i].access);
        snprintf(what, sizeof what, "flags %#x: disposition", map[i].flags);
        EXPECT(what, g_disp, map[i].disp);
        EXPECT("share mode", g_share, 3);
        EXPECT("attributes", g_attrs, 0x80);
        if (fd >= 0)
            T_close(fd);
    }
    unlink("t-pe.tmp");
    int calls = g_createfile_calls;
    EXPECT("access mode 3", T_open("t-pe.tmp", 3), -22);
    EXPECT("access mode 3 makes no CreateFileA call", g_createfile_calls, calls);

    // ---- the same sequence as tests/trap/files.asm ------------------------
    int fd = T_open("t-pe.tmp", 577);
    EXPECT("create", fd >= HBASE, 1);
    EXPECT("write 10", T_write(fd, "0123456789", 10), 10);
    EXPECT("close", T_close(fd), 0);
    fd = T_open("t-pe.tmp", 0);
    EXPECT("reopen", fd >= HBASE, 1);
    EXPECT("read 10", T_read(fd, buf, 10), 10);
    EXPECT("first byte", buf[0], '0');
    EXPECT("seek set 3", T_seek(fd, 3, 0), 3);
    EXPECT("read 2", T_read(fd, buf, 2), 2);
    EXPECT("bytes 3,4", buf[0] == '3' && buf[1] == '4', 1);
    EXPECT("seek end -2", T_seek(fd, -2, 2), 8);
    EXPECT("read 5 gives 2", T_read(fd, buf, 5), 2);
    EXPECT("byte 8", buf[0], '8');
    EXPECT("seek cur 0", T_seek(fd, 0, 1), 10);
    EXPECT("read at EOF", T_read(fd, buf, 4), 0);
    EXPECT("bad whence", T_seek(fd, 0, 7), -22);
    EXPECT("seek before start", T_seek(fd, -100, 0), -22);
    EXPECT("close again", T_close(fd), 0);
    EXPECT("close bad handle", T_close(0x555), -9);
    EXPECT("seek bad handle", T_seek(0x555, 0, 0), -9);
    EXPECT("missing file", T_open("no-such-dir/x", 0), -2);
    EXPECT("CREAT|EXCL on existing", T_open("t-pe.tmp", 193), -17);
    fd = T_open("t-pe.tmp", 1025);
    EXPECT("append open", fd >= HBASE, 1);
    T_seek(fd, 0, 0);
    EXPECT("append write", T_write(fd, "AB", 2), 2);
    T_close(fd);
    fd = T_open("t-pe.tmp", 0);
    EXPECT("size after append", T_seek(fd, 0, 2), 12);
    T_close(fd);
    fd = T_open("t-pe.tmp", 513);
    EXPECT("size after trunc", T_seek(fd, 0, 2), 0);
    T_close(fd);
    unlink("t-pe.tmp");

    // ---- standard streams ------------------------------------------------
    EXPECT("write to stdout", T_write(1, "ok\n", 3), 3);
    EXPECT("write to stderr", T_write(2, "", 0), 0);
    EXPECT("write to stdin", T_write(0, "x", 1), -9);
    EXPECT("read from stdout", T_read(1, buf, 1), -9);
    EXPECT("read from stderr", T_read(2, buf, 1), -9);
    EXPECT("read 0 bytes from stdin", T_read(0, buf, 0), 0);
    calls = g_closehandle_calls;
    EXPECT("close(0)", T_close(0), 0);
    EXPECT("close(1)", T_close(1), 0);
    EXPECT("close(2)", T_close(2), 0);
    EXPECT("closing 0-2 never reaches CloseHandle", g_closehandle_calls, calls);
    EXPECT("lseek on stdin", T_seek(0, 0, 0), -29);
    EXPECT("lseek on stdout", T_seek(1, 0, 0), -29);

    // ---- read/write error mapping ----------------------------------------
    EXPECT("broken pipe is end of input", T_read(0x7777, buf, 4), 0);
    EXPECT("other read failure", T_read(0x7778, buf, 4), -5);
    EXPECT("other write failure", T_write(0x7778, buf, 4), -5);

    // ---- unknown service, exit ------------------------------------------------
    EXPECT("unknown service 0", TRAP(0, 1, 2, 3, "z"), -38);
    EXPECT("unknown service 7", TRAP(7, 1, 2, 3, "z"), -38);
    pid_t pid = fork();
    if (pid == 0)
    {
        TRAP(CORE_SVC_EXIT, 42, 0, 0, 0);
        _exit(77);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    EXPECT("exit status", WIFEXITED(st) ? WEXITSTATUS(st) : -1, 42);

    printf("%s: %d checks, %d failed\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
    return g_fail != 0;
}