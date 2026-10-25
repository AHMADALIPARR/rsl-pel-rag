/* BSM-64 assembler (Milestone 002).
 * Parses the textual assembly grammar in
 * constitution/assembly-grammar.spec for exactly the Milestone 001
 * opcode subset and emits a PQRB file per
 * constitution/binary-abi.spec, using the shared encoder, validator,
 * and SHA-256 from Milestone 001. The assembler never invents its
 * own format: its output is validated with bsm_validate before it
 * is written, so an emitted file the loader rejects is an assembler
 * internal error, never a shipped artifact.
 * Two passes: pass 1 parses lines, collects labels, and records
 * label references unresolved; pass 2 resolves references to
 * absolute instruction indices and emits words.
 * Usage: bsmasm <input.asm> <output.pqrb>
 * Exit statuses: 0 assembled, 1 usage, 2 assembly error,
 * 3 host/internal error. Errors print as
 *   <input>:<line>: error: <message>
 * to stderr; on any error no output file is written.
 * C11, no external libraries. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsm_decoder.h"
#include "bsm_sha256.h"

#define MAX_LINE 1026 /* 1024 bytes + newline + NUL; see spec. */
#define MAX_LABEL_LEN 128
#define MAX_TOKENS 16

static const char *g_infile;

static void asm_error(int line, const char *msg) {
    fprintf(stderr, "%s:%d: error: %s\n", g_infile, line, msg);
}

/* ------------------------------------------------------------------ */
/* Opcode table: the M001 subset only. Later-family mnemonics are not
 * listed here, so they fail as "unknown opcode" by construction. */

typedef struct {
    const char *name;
    uint8_t code;
} op_entry;

static const op_entry OP_TABLE[] = {
    {"MOV", BSM_OP_MOV},     {"LOAD", BSM_OP_LOAD},
    {"STORE", BSM_OP_STORE}, {"ALLOC", BSM_OP_ALLOC},
    {"FREE", BSM_OP_FREE},   {"JMP", BSM_OP_JMP},
    {"BR", BSM_OP_BR},       {"CALL", BSM_OP_CALL},
    {"RET", BSM_OP_RET},     {"TRAP", BSM_OP_TRAP},
    {"ADD", BSM_OP_ADD},     {"SUB", BSM_OP_SUB},
    {"MUL", BSM_OP_MUL},     {"DIV", BSM_OP_DIV},
    {"CMP", BSM_OP_CMP},     {"AND", BSM_OP_AND},
    {"OR", BSM_OP_OR},       {"XOR", BSM_OP_XOR},
    {"SHL", BSM_OP_SHL},     {"SHR", BSM_OP_SHR},
    {"POPCNT", BSM_OP_POPCNT},
};

static int upper_eq(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        char ca = *a;
        char cb = *b;
        if (ca >= 'a' && ca <= 'z') {
            ca = (char)(ca - 'a' + 'A');
        }
        if (cb >= 'a' && cb <= 'z') {
            cb = (char)(cb - 'a' + 'A');
        }
        if (ca != cb) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static int lookup_op(const char *tok, uint8_t *code) {
    size_t i;
    for (i = 0; i < sizeof(OP_TABLE) / sizeof(OP_TABLE[0]); i++) {
        if (upper_eq(tok, OP_TABLE[i].name)) {
            *code = OP_TABLE[i].code;
            return 1;
        }
    }
    return 0;
}

static int is_reserved_name(const char *tok) {
    uint8_t code;
    size_t i;
    int all_digits;
    if (lookup_op(tok, &code)) {
        return 1;
    }
    /* A register name is also reserved as a label. */
    if ((tok[0] != 'r' && tok[0] != 'R') || tok[1] == '\0') {
        return 0;
    }
    all_digits = 1;
    for (i = 1; tok[i] != '\0'; i++) {
        if (tok[i] < '0' || tok[i] > '9') {
            all_digits = 0;
            break;
        }
    }
    return all_digits;
}

/* ------------------------------------------------------------------ */
/* Labels and instructions (pass 1 records). */

typedef struct {
    char name[MAX_LABEL_LEN + 1];
    uint32_t index;
    int line;
} label_entry;

typedef struct {
    uint8_t op;
    uint8_t dest;
    uint8_t src_a;
    uint8_t src_b;
    int32_t imm;
    int has_label; /* Immediate comes from a label reference. */
    char label[MAX_LABEL_LEN + 1];
    int line;
} instr_entry;

static label_entry *g_labels = NULL;
static size_t g_nlabels = 0;
static size_t g_caplabels = 0;
static instr_entry *g_instrs = NULL;
static size_t g_ninstrs = 0;
static size_t g_capinstrs = 0;

static int push_label(const char *name, uint32_t index, int line) {
    size_t i;
    char msg[256];
    for (i = 0; i < g_nlabels; i++) {
        if (strcmp(g_labels[i].name, name) == 0) {
            snprintf(msg, sizeof(msg), "duplicate label '%s'", name);
            asm_error(line, msg);
            return 0;
        }
    }
    if (g_nlabels == g_caplabels) {
        size_t ncap = g_caplabels == 0 ? 32 : g_caplabels * 2;
        label_entry *nb = (label_entry *)realloc(
            g_labels, ncap * sizeof(label_entry));
        if (nb == NULL) {
            return -1;
        }
        g_labels = nb;
        g_caplabels = ncap;
    }
    snprintf(g_labels[g_nlabels].name, sizeof(g_labels[g_nlabels].name),
             "%s", name);
    g_labels[g_nlabels].index = index;
    g_labels[g_nlabels].line = line;
    g_nlabels++;
    return 1;
}

static const label_entry *find_label(const char *name) {
    size_t i;
    for (i = 0; i < g_nlabels; i++) {
        if (strcmp(g_labels[i].name, name) == 0) {
            return &g_labels[i];
        }
    }
    return NULL;
}

static int push_instr(const instr_entry *in) {
    if (g_ninstrs == g_capinstrs) {
        size_t ncap = g_capinstrs == 0 ? 64 : g_capinstrs * 2;
        instr_entry *nb = (instr_entry *)realloc(
            g_instrs, ncap * sizeof(instr_entry));
        if (nb == NULL) {
            return 0;
        }
        g_instrs = nb;
        g_capinstrs = ncap;
    }
    g_instrs[g_ninstrs++] = *in;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Lexical helpers. */

static int is_ident_start(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int is_ident_char(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

/* Parse a register token. Returns 1 on success, 0 with an error
 * already reported on failure. */
static int parse_reg(const char *tok, int line, uint8_t *reg) {
    size_t i;
    long v = 0;
    char msg[256];
    if ((tok[0] != 'r' && tok[0] != 'R') || tok[1] == '\0') {
        snprintf(msg, sizeof(msg), "bad register '%s'", tok);
        asm_error(line, msg);
        return 0;
    }
    for (i = 1; tok[i] != '\0'; i++) {
        if (tok[i] < '0' || tok[i] > '9') {
            snprintf(msg, sizeof(msg), "bad register '%s'", tok);
            asm_error(line, msg);
            return 0;
        }
        v = v * 10 + (tok[i] - '0');
        if (v > 31) {
            snprintf(msg, sizeof(msg), "bad register '%s'", tok);
            asm_error(line, msg);
            return 0;
        }
    }
    *reg = (uint8_t)v;
    return 1;
}

#define IMM_OK 0
#define IMM_MALFORMED 1
#define IMM_RANGE 2

/* Parse a numeric immediate token (no label handling here).
 * Returns IMM_OK and sets *out, or an error class. */
static int parse_number(const char *tok, int32_t *out) {
    const char *p = tok;
    int neg = 0;
    uint64_t mag = 0;
    uint64_t limit;
    if (*p == '-') {
        neg = 1;
        p++;
    }
    if (*p == '\0') {
        return IMM_MALFORMED;
    }
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        if (*p == '\0') {
            return IMM_MALFORMED;
        }
        for (; *p != '\0'; p++) {
            unsigned d;
            if (*p >= '0' && *p <= '9') {
                d = (unsigned)(*p - '0');
            } else if (*p >= 'a' && *p <= 'f') {
                d = (unsigned)(*p - 'a' + 10);
            } else if (*p >= 'A' && *p <= 'F') {
                d = (unsigned)(*p - 'A' + 10);
            } else {
                return IMM_MALFORMED;
            }
            mag = mag * 16 + d;
            if (mag > 0x80000000ull) {
                return IMM_RANGE;
            }
        }
    } else {
        for (; *p != '\0'; p++) {
            if (*p < '0' || *p > '9') {
                return IMM_MALFORMED;
            }
            mag = mag * 10 + (uint64_t)(*p - '0');
            if (mag > 0x80000000ull) {
                return IMM_RANGE;
            }
        }
    }
    limit = neg ? 0x80000000ull : 0x7fffffffull;
    if (mag > limit) {
        return IMM_RANGE;
    }
    if (neg) {
        if (mag == 0x80000000ull) {
            *out = (int32_t)(-2147483647 - 1);
        } else {
            *out = (int32_t)(-(int64_t)mag);
        }
    } else {
        *out = (int32_t)mag;
    }
    return IMM_OK;
}

/* Parse an operand that may be an immediate or a label reference.
 * On success sets either *imm (numeric) or *label (reference) and
 * returns 1. On failure reports the error and returns 0. */
static int parse_imm_or_label(const char *tok, int line, int32_t *imm,
                              char *label, int *has_label) {
    int rc;
    char msg[256];
    size_t i;
    *has_label = 0;
    if (is_ident_start(tok[0])) {
        for (i = 0; tok[i] != '\0'; i++) {
            if (!is_ident_char(tok[i])) {
                snprintf(msg, sizeof(msg), "malformed immediate '%s'",
                         tok);
                asm_error(line, msg);
                return 0;
            }
        }
        if (strlen(tok) > MAX_LABEL_LEN) {
            asm_error(line, "identifier too long");
            return 0;
        }
        snprintf(label, MAX_LABEL_LEN + 1, "%s", tok);
        *has_label = 1;
        return 1;
    }
    rc = parse_number(tok, imm);
    if (rc == IMM_MALFORMED) {
        snprintf(msg, sizeof(msg), "malformed immediate '%s'", tok);
        asm_error(line, msg);
        return 0;
    }
    if (rc == IMM_RANGE) {
        snprintf(msg, sizeof(msg), "immediate out of range '%s'", tok);
        asm_error(line, msg);
        return 0;
    }
    return 1;
}

static int parse_cond(const char *tok, uint8_t *code) {
    static const char *const CONDS[8] = {"EQ", "NE", "MI", "PL",
                                         "CS", "CC", "VS", "VC"};
    int i;
    for (i = 0; i < 8; i++) {
        if (upper_eq(tok, CONDS[i])) {
            *code = (uint8_t)i;
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Operand parsing. */

static int wrong_count(int line, const char *opname, const char *forms) {
    char msg[256];
    snprintf(msg, sizeof(msg), "wrong operand count for %s (expected %s)",
             opname, forms);
    asm_error(line, msg);
    return 0;
}

/* Parse one instruction's operands. toks[0] is the opcode mnemonic,
 * ntok counts all tokens including the mnemonic. On success fills
 * *in (except label resolution) and returns 1. */
static int parse_operands(const char *opname, uint8_t op, char **toks,
                          int ntok, int line, instr_entry *in) {
    int nargs = ntok - 1;
    char **a = toks + 1; /* Operand tokens. */
    int32_t imm;
    char label[MAX_LABEL_LEN + 1];
    int has_label;
    char msg[256];

    memset(in, 0, sizeof(*in));
    in->op = op;
    in->line = line;

    switch (op) {
    case BSM_OP_MOV:
        if (nargs == 2) {
            if (!parse_reg(a[0], line, &in->dest)) {
                return 0;
            }
            if (!parse_reg(a[1], line, &in->src_a)) {
                return 0;
            }
            in->imm = 0;
            return 1;
        }
        if (nargs == 3) {
            if (!parse_reg(a[0], line, &in->dest)) {
                return 0;
            }
            if (!parse_reg(a[1], line, &in->src_a)) {
                return 0;
            }
            if (!parse_imm_or_label(a[2], line, &imm, label,
                                    &has_label)) {
                return 0;
            }
            in->imm = imm;
            in->has_label = has_label;
            if (has_label) {
                snprintf(in->label, sizeof(in->label), "%s", label);
            }
            return 1;
        }
        return wrong_count(line, opname, "D, A | D, A, I");
    case BSM_OP_LOAD:
    case BSM_OP_STORE:
        if (nargs == 3) {
            if (!parse_reg(a[0], line, &in->dest)) {
                return 0;
            }
            if (!parse_reg(a[1], line, &in->src_a)) {
                return 0;
            }
            if (!parse_reg(a[2], line, &in->src_b)) {
                return 0;
            }
            in->imm = 0;
            return 1;
        }
        if (nargs == 4) {
            if (!parse_reg(a[0], line, &in->dest)) {
                return 0;
            }
            if (!parse_reg(a[1], line, &in->src_a)) {
                return 0;
            }
            if (!parse_reg(a[2], line, &in->src_b)) {
                return 0;
            }
            if (!parse_imm_or_label(a[3], line, &imm, label,
                                    &has_label)) {
                return 0;
            }
            in->imm = imm;
            in->has_label = has_label;
            if (has_label) {
                snprintf(in->label, sizeof(in->label), "%s", label);
            }
            return 1;
        }
        return wrong_count(line, opname, "D, A, B | D, A, B, I");
    case BSM_OP_ALLOC:
        if (nargs == 2) {
            if (!parse_reg(a[0], line, &in->dest)) {
                return 0;
            }
            if (!parse_reg(a[1], line, &in->src_a)) {
                return 0;
            }
            in->imm = 0;
            return 1;
        }
        if (nargs == 3) {
            if (!parse_reg(a[0], line, &in->dest)) {
                return 0;
            }
            if (!parse_reg(a[1], line, &in->src_a)) {
                return 0;
            }
            if (!parse_imm_or_label(a[2], line, &imm, label,
                                    &has_label)) {
                return 0;
            }
            in->imm = imm;
            in->has_label = has_label;
            if (has_label) {
                snprintf(in->label, sizeof(in->label), "%s", label);
            }
            return 1;
        }
        return wrong_count(line, opname, "D, A | D, A, I");
    case BSM_OP_FREE:
        if (nargs != 1) {
            return wrong_count(line, opname, "A");
        }
        if (!parse_reg(a[0], line, &in->src_a)) {
            return 0;
        }
        return 1;
    case BSM_OP_JMP:
    case BSM_OP_CALL:
        if (nargs == 1) {
            in->src_a = 0;
            if (!parse_imm_or_label(a[0], line, &imm, label,
                                    &has_label)) {
                return 0;
            }
            in->imm = imm;
            in->has_label = has_label;
            if (has_label) {
                snprintf(in->label, sizeof(in->label), "%s", label);
            }
            return 1;
        }
        if (nargs == 2) {
            if (!parse_reg(a[0], line, &in->src_a)) {
                return 0;
            }
            if (!parse_imm_or_label(a[1], line, &imm, label,
                                    &has_label)) {
                return 0;
            }
            in->imm = imm;
            in->has_label = has_label;
            if (has_label) {
                snprintf(in->label, sizeof(in->label), "%s", label);
            }
            return 1;
        }
        return wrong_count(line, opname, "A, I | I");
    case BSM_OP_BR: {
        uint8_t cond;
        if (nargs == 2) {
            if (!parse_cond(a[0], &cond)) {
                snprintf(msg, sizeof(msg),
                         "unknown condition code '%s'", a[0]);
                asm_error(line, msg);
                return 0;
            }
            in->dest = cond;
            in->src_a = 0;
            if (!parse_imm_or_label(a[1], line, &imm, label,
                                    &has_label)) {
                return 0;
            }
            in->imm = imm;
            in->has_label = has_label;
            if (has_label) {
                snprintf(in->label, sizeof(in->label), "%s", label);
            }
            return 1;
        }
        if (nargs == 3) {
            if (!parse_cond(a[0], &cond)) {
                snprintf(msg, sizeof(msg),
                         "unknown condition code '%s'", a[0]);
                asm_error(line, msg);
                return 0;
            }
            in->dest = cond;
            if (!parse_reg(a[1], line, &in->src_a)) {
                return 0;
            }
            if (!parse_imm_or_label(a[2], line, &imm, label,
                                    &has_label)) {
                return 0;
            }
            in->imm = imm;
            in->has_label = has_label;
            if (has_label) {
                snprintf(in->label, sizeof(in->label), "%s", label);
            }
            return 1;
        }
        return wrong_count(line, opname, "C, A, I | C, I");
    }
    case BSM_OP_RET:
        if (nargs != 0) {
            return wrong_count(line, opname, "(none)");
        }
        return 1;
    case BSM_OP_TRAP: {
        int rc;
        if (nargs != 1) {
            return wrong_count(line, opname, "I");
        }
        if (is_ident_start(a[0][0])) {
            asm_error(line,
                      "trap code must be an immediate, not a label");
            return 0;
        }
        rc = parse_number(a[0], &imm);
        if (rc == IMM_MALFORMED) {
            snprintf(msg, sizeof(msg), "malformed immediate '%s'",
                     a[0]);
            asm_error(line, msg);
            return 0;
        }
        if (rc == IMM_RANGE) {
            snprintf(msg, sizeof(msg), "immediate out of range '%s'",
                     a[0]);
            asm_error(line, msg);
            return 0;
        }
        in->imm = imm;
        return 1;
    }
    case BSM_OP_ADD:
    case BSM_OP_SUB:
    case BSM_OP_MUL:
    case BSM_OP_DIV:
    case BSM_OP_AND:
    case BSM_OP_OR:
    case BSM_OP_XOR:
    case BSM_OP_SHL:
    case BSM_OP_SHR:
        if (nargs != 3) {
            return wrong_count(line, opname, "D, A, B");
        }
        if (!parse_reg(a[0], line, &in->dest)) {
            return 0;
        }
        if (!parse_reg(a[1], line, &in->src_a)) {
            return 0;
        }
        if (!parse_reg(a[2], line, &in->src_b)) {
            return 0;
        }
        return 1;
    case BSM_OP_CMP:
        if (nargs != 2) {
            return wrong_count(line, opname, "A, B");
        }
        if (!parse_reg(a[0], line, &in->src_a)) {
            return 0;
        }
        if (!parse_reg(a[1], line, &in->src_b)) {
            return 0;
        }
        return 1;
    case BSM_OP_POPCNT:
        if (nargs != 2) {
            return wrong_count(line, opname, "D, A");
        }
        if (!parse_reg(a[0], line, &in->dest)) {
            return 0;
        }
        if (!parse_reg(a[1], line, &in->src_a)) {
            return 0;
        }
        return 1;
    default:
        break;
    }
    snprintf(msg, sizeof(msg), "unknown opcode '%s'", opname);
    asm_error(line, msg);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Line parsing (pass 1). Returns 1 on success, 0 on assembly error
 * (already reported), -1 on host error (already reported). Lines
 * that define labels only succeed without emitting. */

static int parse_line_full(char *text, int line) {
    char *p = text;
    char *semi;
    char *toks[MAX_TOKENS];
    int ntok = 0;
    int overflow = 0;
    uint8_t op;
    instr_entry in;
    char msg[256];
    char *tok;

    /* Comment strip: first ';' ends the line. */
    semi = strchr(p, ';');
    if (semi != NULL) {
        *semi = '\0';
    }

    /* Label definitions: "name:" prefixes, possibly several. The
     * lookahead pointer q only commits to p when a label is
     * confirmed; otherwise p is left at the instruction start, so
     * the opcode token is never consumed by the label scan. */
    for (;;) {
        char *q = p;
        char *start;
        char name[MAX_LABEL_LEN + 1];
        size_t len = 0;
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') {
            q++;
        }
        if (!is_ident_start(*q)) {
            p = q;
            break;
        }
        start = q;
        while (is_ident_char(*q)) {
            q++;
            len++;
        }
        if (*q != ':') {
            p = start; /* Not a label; instruction starts here. */
            break;
        }
        if (len > MAX_LABEL_LEN) {
            asm_error(line, "identifier too long");
            return 0;
        }
        memcpy(name, start, len);
        name[len] = '\0';
        if (is_reserved_name(name)) {
            snprintf(msg, sizeof(msg),
                     "label uses a reserved name '%s'", name);
            asm_error(line, msg);
            return 0;
        }
        {
            int rc = push_label(name, (uint32_t)g_ninstrs, line);
            if (rc == -1) {
                asm_error(line, "out of memory");
                return -1;
            }
            if (rc == 0) {
                return 0;
            }
        }
        p = q + 1; /* Past ':'. */
    }

    /* Tokenize the remainder: commas are separators. */
    {
        char *q;
        for (q = p; *q != '\0'; q++) {
            if (*q == ',') {
                *q = ' ';
            }
        }
    }
    tok = strtok(p, " \t\r\n");
    while (tok != NULL) {
        if (ntok < MAX_TOKENS) {
            toks[ntok++] = tok;
        } else {
            overflow = 1;
        }
        tok = strtok(NULL, " \t\r\n");
    }
    if (ntok == 0) {
        return 1; /* Labels-only, blank, or comment-only line. */
    }
    if (toks[0][0] == '.') {
        snprintf(msg, sizeof(msg), "unknown directive '%s'", toks[0]);
        asm_error(line, msg);
        return 0;
    }
    if (!lookup_op(toks[0], &op)) {
        snprintf(msg, sizeof(msg), "unknown opcode '%s'", toks[0]);
        asm_error(line, msg);
        return 0;
    }
    if (overflow) {
        return wrong_count(line, toks[0], "fewer operands");
    }
    if (!parse_operands(toks[0], op, toks, ntok, line, &in)) {
        return 0;
    }
    if (!push_instr(&in)) {
        asm_error(line, "out of memory");
        return -1;
    }
    return 1;
}

int main(int argc, char **argv) {
    FILE *in;
    char buf[MAX_LINE];
    int lineno = 0;
    int rc;
    uint8_t *image = NULL;
    size_t image_len = 0;
    size_t i;
    bsm_image img;
    char err[256];

    if (argc != 3) {
        fprintf(stderr, "usage: bsmasm <input.asm> <output.pqrb>\n");
        return 1;
    }
    g_infile = argv[1];
    in = fopen(argv[1], "r");
    if (in == NULL) {
        fprintf(stderr, "%s: error: cannot read input file\n", argv[1]);
        return 3;
    }
    while (fgets(buf, sizeof(buf), in) != NULL) {
        size_t len = strlen(buf);
        lineno++;
        if (len == sizeof(buf) - 1 && buf[len - 1] != '\n' &&
            !feof(in)) {
            asm_error(lineno, "line too long");
            fclose(in);
            return 2;
        }
        rc = parse_line_full(buf, lineno);
        if (rc == 0) {
            fclose(in);
            return 2;
        }
        if (rc == -1) {
            fclose(in);
            return 3;
        }
    }
    fclose(in);

    if (g_ninstrs == 0) {
        asm_error(0, "no instructions");
        return 2;
    }

    /* Pass 2: resolve label references to instruction indices. */
    for (i = 0; i < g_ninstrs; i++) {
        if (g_instrs[i].has_label) {
            const label_entry *lb = find_label(g_instrs[i].label);
            char msg[256];
            if (lb == NULL) {
                snprintf(msg, sizeof(msg), "undefined label '%s'",
                         g_instrs[i].label);
                asm_error(g_instrs[i].line, msg);
                return 2;
            }
            g_instrs[i].imm = (int32_t)lb->index;
            g_instrs[i].has_label = 0;
        }
    }

    /* Emit: one CODE section, exactly the gen.c layout. */
    image_len = 96 + g_ninstrs * 8;
    image = (uint8_t *)calloc(1, image_len);
    if (image == NULL) {
        fprintf(stderr, "%s: error: out of memory\n", argv[1]);
        return 3;
    }
    memcpy(image, "PQRB", 4);
    bsm_wr16le(image + 4, BSM_FORMAT_VERSION);
    bsm_wr16le(image + 6, BSM_ABI_VERSION);
    bsm_wr32le(image + 8, BSM_TARGET_ARCH);
    bsm_wr32le(image + 12, BSM_HEADER_SIZE);
    bsm_wr64le(image + 16, 96);
    bsm_wr32le(image + 24, 1);
    bsm_wr32le(image + 28, 0);
    bsm_wr32le(image + 64, BSM_SEC_CODE);
    bsm_wr32le(image + 68, BSM_CAP_EXEC | BSM_CAP_READ);
    bsm_wr64le(image + 72, 96);
    bsm_wr64le(image + 80, g_ninstrs * 8);
    bsm_wr64le(image + 88, 8);
    for (i = 0; i < g_ninstrs; i++) {
        uint64_t w = bsm_enc(g_instrs[i].op, g_instrs[i].dest,
                             g_instrs[i].src_a, g_instrs[i].src_b,
                             g_instrs[i].imm);
        bsm_wr64le(image + 96 + i * 8, w);
    }
    bsm_sha256(image + BSM_HEADER_SIZE, image_len - BSM_HEADER_SIZE,
               image + 32);

    /* Self-validation through the loader path before writing. */
    if (bsm_validate(image, image_len, &img, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s: error: internal validation failed: %s\n",
                argv[1], err);
        free(image);
        return 3;
    }
    {
        FILE *out = fopen(argv[2], "wb");
        if (out == NULL) {
            fprintf(stderr, "%s: error: cannot write output file\n",
                    argv[2]);
            free(image);
            return 3;
        }
        if (image_len > 0 &&
            fwrite(image, 1, image_len, out) != image_len) {
            fprintf(stderr, "%s: error: cannot write output file\n",
                    argv[2]);
            fclose(out);
            free(image);
            return 3;
        }
        if (fclose(out) != 0) {
            fprintf(stderr, "%s: error: cannot write output file\n",
                    argv[2]);
            free(image);
            return 3;
        }
    }
    printf("assembled %zu instructions -> %s\n", g_ninstrs, argv[2]);
    free(image);
    return 0;
}
