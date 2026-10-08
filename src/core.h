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

#define CORE_ELF_DISPATCHER_SIZE 64 // core.c checks this at compile time

size_t core_dispatcher_size(const char *target); // 0 = unsupported target
int core_emit_dispatcher(const char *target, uint8_t *out, size_t cap);
// bytes written, or -1

#endif /* CORE_H */