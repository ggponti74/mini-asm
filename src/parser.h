#ifndef MINIASM_PARSER_H
#define MINIASM_PARSER_H

#include <stddef.h>
#include <stdint.h>

#include "opcodes.h"
#include "registers.h"

#define MAX_LINE_LEN 128

typedef enum {
    OPERAND_IMMEDIATE,
    OPERAND_REGISTER
} OperandKind;

typedef struct {
    OperandKind kind;
    int32_t immediate;  /* valid when kind == OPERAND_IMMEDIATE */
    int reg_index;       /* valid when kind == OPERAND_REGISTER, 0-7 */
} Operand;

typedef struct {
    Opcode opcode;
    Size size;
    Operand src;
    Operand dst;
    int line_number;
    char source_line[MAX_LINE_LEN];
} Instruction;

typedef struct {
    Instruction *items;
    size_t count;
    size_t capacity;
} Program;

typedef enum {
    PARSE_OK,     /* instruction parsed into *out */
    PARSE_SKIP,   /* blank line or comment-only line; nothing to execute */
    PARSE_ERROR   /* syntax error; message written to errbuf */
} ParseResult;

/* Parse one operand ("#5", "d0", ...). Returns 1 on success, 0 on failure. */
int parse_operand(const char *text, Operand *out);

/* Parse a single source line. On PARSE_OK, *out is filled in. On
 * PARSE_ERROR, a human-readable message is written to errbuf. */
ParseResult parse_line(const char *line, int line_number, Instruction *out,
                        char *errbuf, size_t errbuf_size);

/* Parse a whole program (newline-separated source). Returns 1 on success
 * with *out filled in (caller must program_free it), 0 on the first
 * error encountered (message written to errbuf). */
int parse_program(const char *source, Program *out, char *errbuf, size_t errbuf_size);

void program_free(Program *prog);

#endif /* MINIASM_PARSER_H */
