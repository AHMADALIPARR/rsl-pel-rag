/* BSM-64 fixture generator.
 * Builds every fixture binary in fixtures/ from instruction words,
 * computes the real SHA-256 content digest for each file, and
 * validates each image with the same validator the loader uses
 * before writing it: valid fixtures must validate, malformed
 * fixtures (except the illegal-opcode one, whose format is valid
 * and whose failure is a run-time trap) must NOT validate.
 * Malformed variants are produced deliberately here, never by
 * accident. Usage: gen <output-directory>
 * No timestamps, no randomness: re-running produces byte-identical
 * files. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsm_decoder.h"
#include "bsm_sha256.h"

static uint64_t E(uint8_t op, uint8_t d, uint8_t a, uint8_t b,
                  int32_t imm) {
    return bsm_enc(op, d, a, b, imm);
}

static void put(uint64_t *w, size_t *n, uint64_t word) {
    w[(*n)++] = word;
}

static void recompute_digest(uint8_t *buf, size_t len) {
    bsm_sha256(buf + BSM_HEADER_SIZE, len - BSM_HEADER_SIZE, buf + 32);
}

/* Build a single-code-section image. Returns a malloc'd buffer. */
static uint8_t *build_single(const uint64_t *words, size_t n, size_t *len_out) {
    size_t len = 96 + n * 8;
    uint8_t *buf = (uint8_t *)calloc(1, len);
    size_t i;
    if (buf == NULL) {
        fprintf(stderr, "gen: out of memory\n");
        exit(1);
    }
    memcpy(buf, "PQRB", 4);
    bsm_wr16le(buf + 4, BSM_FORMAT_VERSION);
    bsm_wr16le(buf + 6, BSM_ABI_VERSION);
    bsm_wr32le(buf + 8, BSM_TARGET_ARCH);
    bsm_wr32le(buf + 12, BSM_HEADER_SIZE);
    bsm_wr64le(buf + 16, 96);
    bsm_wr32le(buf + 24, 1);
    bsm_wr32le(buf + 28, 0);
    bsm_wr32le(buf + 64, BSM_SEC_CODE);
    bsm_wr32le(buf + 68, BSM_CAP_EXEC | BSM_CAP_READ);
    bsm_wr64le(buf + 72, 96);
    bsm_wr64le(buf + 80, n * 8);
    bsm_wr64le(buf + 88, 8);
    for (i = 0; i < n; i++) {
        bsm_wr64le(buf + 96 + i * 8, words[i]);
    }
    recompute_digest(buf, len);
    *len_out = len;
    return buf;
}

static void write_file(const char *dir, const char *name,
                       const uint8_t *buf, size_t len) {
    char path[1024];
    FILE *f;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "gen: cannot write %s\n", path);
        exit(1);
    }
    if (len > 0 && fwrite(buf, 1, len, f) != len) {
        fprintf(stderr, "gen: write failed %s\n", path);
        fclose(f);
        exit(1);
    }
    fclose(f);
    printf("wrote %s (%zu bytes)\n", name, len);
}

/* Write a valid fixture: the image must validate. */
static void valid_fixture(const char *dir, const char *name,
                          const uint64_t *words, size_t n) {
    size_t len = 0;
    uint8_t *buf = build_single(words, n, &len);
    bsm_image img;
    char err[256];
    if (bsm_validate(buf, len, &img, err, sizeof(err)) != 0) {
        fprintf(stderr, "gen: valid fixture %s failed validation: %s\n",
                name, err);
        free(buf);
        exit(1);
    }
    write_file(dir, name, buf, len);
    free(buf);
}

/* Write a malformed fixture: the image must NOT validate. */
static void malformed_fixture(const char *dir, const char *name,
                              uint8_t *buf, size_t len) {
    bsm_image img;
    char err[256];
    if (bsm_validate(buf, len, &img, err, sizeof(err)) == 0) {
        fprintf(stderr, "gen: malformed fixture %s unexpectedly "
                        "validated\n",
                name);
        free(buf);
        exit(1);
    }
    printf("malformed %s rejected as expected (%s)\n", name, err);
    write_file(dir, name, buf, len);
    free(buf);
}

#define PROG_BEGIN uint64_t w[64]; size_t n = 0
#define PROG_END(name) valid_fixture(dir, name, w, n)

static void gen_valid(const char *dir) {
    { /* arith: 40 + 2 = 42; 42 * 10 = 420; 420 - 42 = 378. */
        PROG_BEGIN;
        put(w, &n, E(BSM_OP_MOV, 1, 0, 0, 40));
        put(w, &n, E(BSM_OP_MOV, 2, 0, 0, 2));
        put(w, &n, E(BSM_OP_ADD, 3, 1, 2, 0));
        put(w, &n, E(BSM_OP_MOV, 4, 0, 0, 10));
        put(w, &n, E(BSM_OP_MUL, 5, 3, 4, 0));
        put(w, &n, E(BSM_OP_SUB, 6, 5, 3, 0));
        put(w, &n, E(BSM_OP_RET, 0, 0, 0, 0));
        PROG_END("arith.pqrb");
    }
    { /* loop: sum 1..10 = 55. */
        PROG_BEGIN;
        put(w, &n, E(BSM_OP_MOV, 1, 0, 0, 0));
        put(w, &n, E(BSM_OP_MOV, 2, 0, 0, 1));
        put(w, &n, E(BSM_OP_MOV, 3, 0, 0, 11));
        put(w, &n, E(BSM_OP_MOV, 4, 0, 0, 1));
        put(w, &n, E(BSM_OP_ADD, 1, 1, 2, 0));
        put(w, &n, E(BSM_OP_ADD, 2, 2, 4, 0));
        put(w, &n, E(BSM_OP_CMP, 0, 2, 3, 0));
        put(w, &n, E(BSM_OP_BR, 1, 0, 0, 4)); /* NE -> 4 */
        put(w, &n, E(BSM_OP_RET, 0, 0, 0, 0));
        PROG_END("loop.pqrb");
    }
    { /* callret: 21 doubled by a subroutine = 42. */
        PROG_BEGIN;
        put(w, &n, E(BSM_OP_MOV, 1, 0, 0, 21));
        put(w, &n, E(BSM_OP_CALL, 0, 0, 0, 4));
        put(w, &n, E(BSM_OP_MOV, 3, 2, 0, 0));
        put(w, &n, E(BSM_OP_RET, 0, 0, 0, 0));
        put(w, &n, E(BSM_OP_ADD, 2, 1, 1, 0));
        put(w, &n, E(BSM_OP_RET, 0, 0, 0, 0));
        PROG_END("callret.pqrb");
    }
    { /* memory: build 0x1122334455667788, store it, load it back. */
        PROG_BEGIN;
        put(w, &n, E(BSM_OP_MOV, 2, 0, 0, 0x11223344));
        put(w, &n, E(BSM_OP_MOV, 4, 0, 0, 32));
        put(w, &n, E(BSM_OP_SHL, 2, 2, 4, 0));
        put(w, &n, E(BSM_OP_MOV, 5, 0, 0, 0x55667788));
        put(w, &n, E(BSM_OP_OR, 2, 2, 5, 0));
        put(w, &n, E(BSM_OP_ALLOC, 1, 0, 0, 16));
        put(w, &n, E(BSM_OP_STORE, 2, 1, 0, 0));
        put(w, &n, E(BSM_OP_LOAD, 3, 1, 0, 0));
        put(w, &n, E(BSM_OP_FREE, 0, 1, 0, 0));
        put(w, &n, E(BSM_OP_RET, 0, 0, 0, 0));
        PROG_END("memory.pqrb");
    }
    { /* bench: count to 200000; 600004 instructions total. */
        PROG_BEGIN;
        put(w, &n, E(BSM_OP_MOV, 1, 0, 0, 0));
        put(w, &n, E(BSM_OP_MOV, 3, 0, 0, 200000));
        put(w, &n, E(BSM_OP_MOV, 4, 0, 0, 1));
        put(w, &n, E(BSM_OP_ADD, 1, 1, 4, 0));
        put(w, &n, E(BSM_OP_CMP, 0, 1, 3, 0));
        put(w, &n, E(BSM_OP_BR, 1, 0, 0, 3)); /* NE -> 3 */
        put(w, &n, E(BSM_OP_RET, 0, 0, 0, 0));
        PROG_END("bench.pqrb");
    }
    { /* wrap: INT64_MAX + 1 wraps to INT64_MIN. */
        PROG_BEGIN;
        put(w, &n, E(BSM_OP_MOV, 1, 0, 0, -1));
        put(w, &n, E(BSM_OP_MOV, 2, 0, 0, 1));
        put(w, &n, E(BSM_OP_SHR, 1, 1, 2, 0));
        put(w, &n, E(BSM_OP_ADD, 3, 1, 2, 0));
        put(w, &n, E(BSM_OP_RET, 0, 0, 0, 0));
        PROG_END("wrap.pqrb");
    }
    { /* divzero. */
        PROG_BEGIN;
        put(w, &n, E(BSM_OP_MOV, 1, 0, 0, 10));
        put(w, &n, E(BSM_OP_MOV, 2, 0, 0, 0));
        put(w, &n, E(BSM_OP_DIV, 3, 1, 2, 0));
        put(w, &n, E(BSM_OP_RET, 0, 0, 0, 0));
        PROG_END("divzero.pqrb");
    }
    { /* divovf: INT64_MIN / -1. */
        PROG_BEGIN;
        put(w, &n, E(BSM_OP_MOV, 1, 0, 0, 1));
        put(w, &n, E(BSM_OP_MOV, 2, 0, 0, 63));
        put(w, &n, E(BSM_OP_SHL, 1, 1, 2, 0));
        put(w, &n, E(BSM_OP_MOV, 3, 0, 0, -1));
        put(w, &n, E(BSM_OP_DIV, 4, 1, 3, 0));
        put(w, &n, E(BSM_OP_RET, 0, 0, 0, 0));
        PROG_END("divovf.pqrb");
    }
    { /* shift0: 0x1234 << 0. */
        PROG_BEGIN;
        put(w, &n, E(BSM_OP_MOV, 1, 0, 0, 0x1234));
        put(w, &n, E(BSM_OP_MOV, 2, 0, 0, 0));
        put(w, &n, E(BSM_OP_SHL, 3, 1, 2, 0));
        put(w, &n, E(BSM_OP_RET, 0, 0, 0, 0));
        PROG_END("shift0.pqrb");
    }
    { /* shift63: 1 << 63. */
        PROG_BEGIN;
        put(w, &n, E(BSM_OP_MOV, 1, 0, 0, 1));
        put(w, &n, E(BSM_OP_MOV, 2, 0, 0, 63));
        put(w, &n, E(BSM_OP_SHL, 3, 1, 2, 0));
        put(w, &n, E(BSM_OP_RET, 0, 0, 0, 0));
        PROG_END("shift63.pqrb");
    }
    { /* shift64: count 64 mod 64 = 0, value unchanged. */
        PROG_BEGIN;
        put(w, &n, E(BSM_OP_MOV, 1, 0, 0, 0x1234));
        put(w, &n, E(BSM_OP_MOV, 2, 0, 0, 64));
        put(w, &n, E(BSM_OP_SHL, 3, 1, 2, 0));
        put(w, &n, E(BSM_OP_RET, 0, 0, 0, 0));
        PROG_END("shift64.pqrb");
    }
    { /* shift65: count 65 mod 64 = 1, value doubled. */
        PROG_BEGIN;
        put(w, &n, E(BSM_OP_MOV, 1, 0, 0, 0x1234));
        put(w, &n, E(BSM_OP_MOV, 2, 0, 0, 65));
        put(w, &n, E(BSM_OP_SHL, 3, 1, 2, 0));
        put(w, &n, E(BSM_OP_RET, 0, 0, 0, 0));
        PROG_END("shift65.pqrb");
    }
    { /* popcnt: popcount(43) = 4, popcount(-1) = 64. */
        PROG_BEGIN;
        put(w, &n, E(BSM_OP_MOV, 1, 0, 0, 43));
        put(w, &n, E(BSM_OP_POPCNT, 2, 1, 0, 0));
        put(w, &n, E(BSM_OP_MOV, 3, 0, 0, -1));
        put(w, &n, E(BSM_OP_POPCNT, 4, 3, 0, 0));
        put(w, &n, E(BSM_OP_RET, 0, 0, 0, 0));
        PROG_END("popcnt.pqrb");
    }
    { /* branchtaken: equal operands take the EQ branch. */
        PROG_BEGIN;
        put(w, &n, E(BSM_OP_MOV, 1, 0, 0, 5));
        put(w, &n, E(BSM_OP_MOV, 2, 0, 0, 5));
        put(w, &n, E(BSM_OP_CMP, 0, 1, 2, 0));
        put(w, &n, E(BSM_OP_BR, 0, 0, 0, 6)); /* EQ -> 6 */
        put(w, &n, E(BSM_OP_MOV, 3, 0, 0, 99));
        put(w, &n, E(BSM_OP_JMP, 0, 0, 0, 7));
        put(w, &n, E(BSM_OP_MOV, 3, 0, 0, 7));
        put(w, &n, E(BSM_OP_RET, 0, 0, 0, 0));
        PROG_END("branchtaken.pqrb");
    }
    { /* branchnottaken: unequal operands fall through. */
        PROG_BEGIN;
        put(w, &n, E(BSM_OP_MOV, 1, 0, 0, 5));
        put(w, &n, E(BSM_OP_MOV, 2, 0, 0, 6));
        put(w, &n, E(BSM_OP_CMP, 0, 1, 2, 0));
        put(w, &n, E(BSM_OP_BR, 0, 0, 0, 6)); /* EQ -> 6, not taken */
        put(w, &n, E(BSM_OP_MOV, 3, 0, 0, 9));
        put(w, &n, E(BSM_OP_JMP, 0, 0, 0, 7));
        put(w, &n, E(BSM_OP_MOV, 3, 0, 0, 99));
        put(w, &n, E(BSM_OP_RET, 0, 0, 0, 0));
        PROG_END("branchnottaken.pqrb");
    }
    { /* oobload: load at offset 8 from an 8-byte allocation. */
        PROG_BEGIN;
        put(w, &n, E(BSM_OP_ALLOC, 1, 0, 0, 8));
        put(w, &n, E(BSM_OP_LOAD, 2, 1, 0, 8));
        put(w, &n, E(BSM_OP_RET, 0, 0, 0, 0));
        PROG_END("oobload.pqrb");
    }
    { /* oobstore: store at offset 8 into an 8-byte allocation. */
        PROG_BEGIN;
        put(w, &n, E(BSM_OP_MOV, 2, 0, 0, 7));
        put(w, &n, E(BSM_OP_ALLOC, 1, 0, 0, 8));
        put(w, &n, E(BSM_OP_STORE, 2, 1, 0, 8));
        put(w, &n, E(BSM_OP_RET, 0, 0, 0, 0));
        PROG_END("oobstore.pqrb");
    }
    { /* unallochandle: load through handle 0, never allocated. */
        PROG_BEGIN;
        put(w, &n, E(BSM_OP_LOAD, 2, 0, 0, 0));
        put(w, &n, E(BSM_OP_RET, 0, 0, 0, 0));
        PROG_END("unallochandle.pqrb");
    }
    { /* doublefree. */
        PROG_BEGIN;
        put(w, &n, E(BSM_OP_ALLOC, 1, 0, 0, 8));
        put(w, &n, E(BSM_OP_FREE, 0, 1, 0, 0));
        put(w, &n, E(BSM_OP_FREE, 0, 1, 0, 0));
        put(w, &n, E(BSM_OP_RET, 0, 0, 0, 0));
        PROG_END("doublefree.pqrb");
    }
    { /* useafterfree. */
        PROG_BEGIN;
        put(w, &n, E(BSM_OP_ALLOC, 1, 0, 0, 8));
        put(w, &n, E(BSM_OP_FREE, 0, 1, 0, 0));
        put(w, &n, E(BSM_OP_LOAD, 2, 1, 0, 0));
        put(w, &n, E(BSM_OP_RET, 0, 0, 0, 0));
        PROG_END("useafterfree.pqrb");
    }
    { /* jumpoutside: jump to instruction 999. */
        PROG_BEGIN;
        put(w, &n, E(BSM_OP_JMP, 0, 0, 0, 999));
        put(w, &n, E(BSM_OP_RET, 0, 0, 0, 0));
        PROG_END("jumpoutside.pqrb");
    }
    { /* trap: explicit guest trap with code 7. */
        PROG_BEGIN;
        put(w, &n, E(BSM_OP_TRAP, 0, 0, 0, 7));
        put(w, &n, E(BSM_OP_RET, 0, 0, 0, 0));
        PROG_END("trap.pqrb");
    }
    { /* infinite: unconditional self-jump, for the budget test. */
        PROG_BEGIN;
        put(w, &n, E(BSM_OP_JMP, 0, 0, 0, 0));
        PROG_END("infinite.pqrb");
    }
}

static void gen_malformed(const char *dir) {
    uint64_t arith[7];
    size_t an = 0;
    size_t len = 0;
    uint8_t *base;
    put(arith, &an, E(BSM_OP_MOV, 1, 0, 0, 40));
    put(arith, &an, E(BSM_OP_MOV, 2, 0, 0, 2));
    put(arith, &an, E(BSM_OP_ADD, 3, 1, 2, 0));
    put(arith, &an, E(BSM_OP_MOV, 4, 0, 0, 10));
    put(arith, &an, E(BSM_OP_MUL, 5, 3, 4, 0));
    put(arith, &an, E(BSM_OP_SUB, 6, 5, 3, 0));
    put(arith, &an, E(BSM_OP_RET, 0, 0, 0, 0));
    base = build_single(arith, an, &len);

    { /* badmagic: header bytes are not hashed, no digest repair. */
        uint8_t *buf = (uint8_t *)malloc(len);
        memcpy(buf, base, len);
        memcpy(buf, "XXXX", 4);
        malformed_fixture(dir, "badmagic.pqrb", buf, len);
    }
    { /* badversion. */
        uint8_t *buf = (uint8_t *)malloc(len);
        memcpy(buf, base, len);
        bsm_wr16le(buf + 4, 99);
        malformed_fixture(dir, "badversion.pqrb", buf, len);
    }
    { /* truncated: only the first 32 bytes of the file. */
        uint8_t *buf = (uint8_t *)malloc(32);
        memcpy(buf, base, 32);
        malformed_fixture(dir, "truncated.pqrb", buf, 32);
    }
    { /* sectionoob: section offset past end of file; digest repaired
       * so that only the bounds check fails. */
        uint8_t *buf = (uint8_t *)malloc(len);
        memcpy(buf, base, len);
        bsm_wr64le(buf + 72, (uint64_t)len + 1000);
        recompute_digest(buf, len);
        malformed_fixture(dir, "sectionoob.pqrb", buf, len);
    }
    { /* entryoutside: entry past the end of the code section. */
        uint8_t *buf = (uint8_t *)malloc(len);
        memcpy(buf, base, len);
        bsm_wr64le(buf + 16, 96 + an * 8 + 8);
        malformed_fixture(dir, "entryoutside.pqrb", buf, len);
    }
    { /* digestmismatch: flip a digest byte, nothing else. */
        uint8_t *buf = (uint8_t *)malloc(len);
        memcpy(buf, base, len);
        buf[32] ^= 0xff;
        malformed_fixture(dir, "digestmismatch.pqrb", buf, len);
    }
    { /* badalignment: alignment 3 is not a power of two; digest
       * repaired so only the alignment check fails. */
        uint8_t *buf = (uint8_t *)malloc(len);
        memcpy(buf, base, len);
        bsm_wr64le(buf + 88, 3);
        recompute_digest(buf, len);
        malformed_fixture(dir, "badalignment.pqrb", buf, len);
    }
    { /* illegalopcode: format-valid, opcode 0x40 (Region range).
       * This one must VALIDATE; the VM traps at run time. */
        uint8_t *buf = (uint8_t *)malloc(len);
        bsm_image img;
        char err[256];
        memcpy(buf, base, len);
        buf[96 + 7] = 0x40; /* Most significant byte = opcode. */
        recompute_digest(buf, len);
        if (bsm_validate(buf, len, &img, err, sizeof(err)) != 0) {
            fprintf(stderr, "gen: illegalopcode fixture failed "
                            "validation: %s\n",
                    err);
            free(buf);
            free(base);
            exit(1);
        }
        write_file(dir, "illegalopcode.pqrb", buf, len);
        free(buf);
    }
    { /* overlapping: a second section overlapping the code section. */
        size_t olen = 128 + an * 8;
        uint8_t *buf = (uint8_t *)calloc(1, olen);
        size_t i;
        memcpy(buf, "PQRB", 4);
        bsm_wr16le(buf + 4, BSM_FORMAT_VERSION);
        bsm_wr16le(buf + 6, BSM_ABI_VERSION);
        bsm_wr32le(buf + 8, BSM_TARGET_ARCH);
        bsm_wr32le(buf + 12, BSM_HEADER_SIZE);
        bsm_wr64le(buf + 16, 128);
        bsm_wr32le(buf + 24, 2);
        bsm_wr32le(buf + 28, 0);
        bsm_wr32le(buf + 64, BSM_SEC_CODE);
        bsm_wr32le(buf + 68, BSM_CAP_EXEC | BSM_CAP_READ);
        bsm_wr64le(buf + 72, 128);
        bsm_wr64le(buf + 80, an * 8);
        bsm_wr64le(buf + 88, 8);
        bsm_wr32le(buf + 96, BSM_SEC_DATA);
        bsm_wr32le(buf + 100, BSM_CAP_READ);
        bsm_wr64le(buf + 104, 128);
        bsm_wr64le(buf + 112, 8);
        bsm_wr64le(buf + 120, 8);
        for (i = 0; i < an; i++) {
            bsm_wr64le(buf + 128 + i * 8, arith[i]);
        }
        recompute_digest(buf, olen);
        malformed_fixture(dir, "overlapping.pqrb", buf, olen);
    }
    free(base);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: gen <output-directory>\n");
        return 1;
    }
    gen_valid(argv[1]);
    gen_malformed(argv[1]);
    return 0;
}
