#include "parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- small string helpers ------------------------------------------- */

static void trim_into(const char *src, char *dst, size_t dst_size) {
    while (isspace((unsigned char)*src)) src++;
    const char *end = src + strlen(src);
    while (end > src && isspace((unsigned char)*(end - 1))) end--;

    size_t len = (size_t)(end - src);
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void strip_comment(char *line) {
    char *semi = strchr(line, ';');
    if (semi) *semi = '\0';
}

static void lower_inplace(char *s) {
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

/* ---- operand parsing --------------------------------------------------
 * "#5", "#-5", "#0x1F", "#$1F"  -> immediate
 * "d0".."d7" (case-insensitive) -> register
 */

static int parse_immediate(const char *text, int32_t *out) {
    int negative = 0;
    const char *p = text;
    if (*p == '-') { negative = 1; p++; }
    if (*p == '\0') return 0;

    long value;
    char *endptr;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        value = strtol(p + 2, &endptr, 16);
    } else if (p[0] == '$') {
        value = strtol(p + 1, &endptr, 16);
    } else {
        value = strtol(p, &endptr, 10);
    }
    if (endptr == p || *endptr != '\0') return 0; /* no digits, or trailing garbage */

    *out = negative ? -(int32_t)value : (int32_t)value;
    return 1;
}

int parse_operand(const char *text, Operand *out) {
    char buf[64];
    trim_into(text, buf, sizeof buf);
    if (buf[0] == '\0') return 0;

    if (buf[0] == '#') {
        int32_t value;
        if (!parse_immediate(buf + 1, &value)) return 0;
        out->kind = OPERAND_IMMEDIATE;
        out->immediate = value;
        return 1;
    }

    if ((buf[0] == 'd' || buf[0] == 'D') &&
        buf[1] >= '0' && buf[1] <= '7' &&
        buf[2] == '\0') {
        out->kind = OPERAND_REGISTER;
        out->reg_index = buf[1] - '0';
        return 1;
    }

    return 0;
}

/* ---- line parsing ------------------------------------------------------ */

ParseResult parse_line(const char *raw_line, int line_number, Instruction *out,
                        char *errbuf, size_t errbuf_size) {
    char line[MAX_LINE_LEN];
    trim_into(raw_line, line, sizeof line);
    strip_comment(line);

    char code[MAX_LINE_LEN];
    trim_into(line, code, sizeof code);
    if (code[0] == '\0') return PARSE_SKIP;

    /* Split "mnemonic[.size] operand, operand" on the first space. */
    char head[32];
    const char *space = strchr(code, ' ');
    if (!space) {
        snprintf(errbuf, errbuf_size, "line %d: missing operands: \"%s\"", line_number, raw_line);
        return PARSE_ERROR;
    }
    size_t head_len = (size_t)(space - code);
    if (head_len >= sizeof head) head_len = sizeof head - 1;
    memcpy(head, code, head_len);
    head[head_len] = '\0';

    char rest[MAX_LINE_LEN];
    trim_into(space + 1, rest, sizeof rest);
    if (rest[0] == '\0') {
        snprintf(errbuf, errbuf_size, "line %d: missing operands: \"%s\"", line_number, raw_line);
        return PARSE_ERROR;
    }

    /* mnemonic[.suffix] */
    char mnemonic[16];
    char suffix = '\0';
    char *dot = strchr(head, '.');
    if (dot) {
        size_t mnem_len = (size_t)(dot - head);
        if (mnem_len >= sizeof mnemonic) mnem_len = sizeof mnemonic - 1;
        memcpy(mnemonic, head, mnem_len);
        mnemonic[mnem_len] = '\0';

        if (dot[1] == '\0' || dot[2] != '\0') {
            snprintf(errbuf, errbuf_size,
                     "line %d: invalid size suffix in \"%s\"", line_number, raw_line);
            return PARSE_ERROR;
        }
        suffix = (char)tolower((unsigned char)dot[1]);
        if (suffix != 'b' && suffix != 'w' && suffix != 'l') {
            snprintf(errbuf, errbuf_size,
                     "line %d: unknown size suffix '.%c' in \"%s\"", line_number, suffix, raw_line);
            return PARSE_ERROR;
        }
    } else {
        size_t mnem_len = strlen(head);
        if (mnem_len >= sizeof mnemonic) mnem_len = sizeof mnemonic - 1;
        memcpy(mnemonic, head, mnem_len);
        mnemonic[mnem_len] = '\0';
    }
    lower_inplace(mnemonic);

    Opcode opcode;
    Size size;
    if (!opcode_lookup(mnemonic, suffix, &opcode, &size)) {
        snprintf(errbuf, errbuf_size,
                 "line %d: unknown opcode \"%s\" in: \"%s\"", line_number, mnemonic, raw_line);
        return PARSE_ERROR;
    }

    /* Split operands on the (single) comma. */
    char *comma = strchr(rest, ',');
    if (!comma) {
        snprintf(errbuf, errbuf_size,
                 "line %d: expected two comma-separated operands: \"%s\"", line_number, raw_line);
        return PARSE_ERROR;
    }
    *comma = '\0';
    const char *src_text = rest;
    const char *dst_text = comma + 1;

    Operand src, dst;
    if (!parse_operand(src_text, &src)) {
        snprintf(errbuf, errbuf_size,
                 "line %d: unrecognized operand \"%s\"", line_number, src_text);
        return PARSE_ERROR;
    }
    if (!parse_operand(dst_text, &dst)) {
        snprintf(errbuf, errbuf_size,
                 "line %d: unrecognized operand \"%s\"", line_number, dst_text);
        return PARSE_ERROR;
    }

    out->opcode = opcode;
    out->size = size;
    out->src = src;
    out->dst = dst;
    out->line_number = line_number;
    trim_into(raw_line, out->source_line, sizeof out->source_line);
    return PARSE_OK;
}

/* ---- program parsing (growable array of instructions) ------------------ */

static int program_push(Program *prog, const Instruction *instr) {
    if (prog->count == prog->capacity) {
        size_t new_capacity = prog->capacity == 0 ? 16 : prog->capacity * 2;
        Instruction *grown = realloc(prog->items, new_capacity * sizeof(Instruction));
        if (!grown) return 0;
        prog->items = grown;
        prog->capacity = new_capacity;
    }
    prog->items[prog->count++] = *instr;
    return 1;
}

int parse_program(const char *source, Program *out, char *errbuf, size_t errbuf_size) {
    out->items = NULL;
    out->count = 0;
    out->capacity = 0;

    size_t source_len = strlen(source);
    char *copy = malloc(source_len + 1);
    if (!copy) {
        snprintf(errbuf, errbuf_size, "out of memory");
        return 0;
    }
    memcpy(copy, source, source_len + 1);

    int line_number = 0;
    int ok = 1;
    char *line = strtok(copy, "\n");
    while (line) {
        line_number++;
        /* strip a trailing \r for CRLF source files */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\r') line[len - 1] = '\0';

        Instruction instr;
        ParseResult result = parse_line(line, line_number, &instr, errbuf, errbuf_size);
        if (result == PARSE_ERROR) {
            ok = 0;
            break;
        }
        if (result == PARSE_OK) {
            if (!program_push(out, &instr)) {
                snprintf(errbuf, errbuf_size, "out of memory");
                ok = 0;
                break;
            }
        }
        line = strtok(NULL, "\n");
    }

    free(copy);
    if (!ok) {
        program_free(out);
    }
    return ok;
}

void program_free(Program *prog) {
    free(prog->items);
    prog->items = NULL;
    prog->count = 0;
    prog->capacity = 0;
}
