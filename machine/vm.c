/* bsm64: the BSM-64 Milestone 001 virtual machine.
 * Usage: bsm64 [--trace] [--disasm] [--max-instructions N] <file.pqrb>
 * Exit statuses (see constitution/instruction-set.spec):
 *   0  normal halt (RET on an empty return stack, or falling off the
 *      end of the code section)
 *   1  usage error
 *   2  load / validation error (the file is rejected before any
 *      guest instruction runs)
 *   3  guest trap
 *   4  instruction budget exceeded
 *   5  host internal error
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsm_decoder.h"
#include "bsm_memory.h"
#include "bsm_registers.h"

#define BSM_RETSTACK_SIZE 1024
#define BSM_DEFAULT_BUDGET 1000000ull

static void usage(void) {
    fprintf(stderr,
            "usage: bsm64 [--trace] [--disasm] "
            "[--max-instructions N] <file.pqrb>\n");
}

static void dump_regs(const bsm_regs *r) {
    int i;
    printf("flags: Z=%d N=%d C=%d V=%d\n", r->z, r->n, r->c, r->v);
    for (i = 0; i < BSM_NREGS; i++) {
        printf("R%d=0x%016" PRIx64 "\n", i, r->gpr[i]);
    }
    printf("sp=%" PRIu64 "\n", r->sp);
}

static int reg_ok(uint8_t v) { return v < BSM_NREGS; }

static uint64_t popcount64(uint64_t v) {
    uint64_t n = 0;
    while (v != 0) {
        n += v & 1u;
        v >>= 1;
    }
    return n;
}

int main(int argc, char **argv) {
    const char *path = NULL;
    int trace = 0;
    int disasm_only = 0;
    uint64_t budget = BSM_DEFAULT_BUDGET;
    uint8_t *data = NULL;
    size_t len = 0;
    FILE *f = NULL;
    long fsize = 0;
    bsm_image img;
    char err[256];
    bsm_regs regs;
    bsm_mem mem;
    uint64_t retstack[BSM_RETSTACK_SIZE];
    size_t depth = 0;
    uint64_t executed = 0;
    size_t ninstr = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--trace") == 0) {
            trace = 1;
        } else if (strcmp(argv[i], "--disasm") == 0) {
            disasm_only = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else if (strcmp(argv[i], "--max-instructions") == 0) {
            char *end = NULL;
            if (i + 1 >= argc) {
                usage();
                return 1;
            }
            budget = strtoull(argv[++i], &end, 10);
            if (end == NULL || *end != '\0' || budget == 0) {
                usage();
                return 1;
            }
        } else if (strncmp(argv[i], "--max-instructions=", 19) == 0) {
            char *end = NULL;
            budget = strtoull(argv[i] + 19, &end, 10);
            if (end == NULL || *end != '\0' || budget == 0) {
                usage();
                return 1;
            }
        } else if (argv[i][0] == '-') {
            usage();
            return 1;
        } else if (path == NULL) {
            path = argv[i];
        } else {
            usage();
            return 1;
        }
    }
    if (path == NULL) {
        usage();
        return 1;
    }

    f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "load error: cannot open %s\n", path);
        return 2;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        fprintf(stderr, "load error: cannot seek %s\n", path);
        return 2;
    }
    fsize = ftell(f);
    if (fsize < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        fprintf(stderr, "load error: cannot read %s\n", path);
        return 2;
    }
    len = (size_t)fsize;
    data = (uint8_t *)malloc(len == 0 ? 1 : len);
    if (data == NULL) {
        fclose(f);
        fprintf(stderr, "internal error: out of memory\n");
        return 5;
    }
    if (len > 0 && fread(data, 1, len, f) != len) {
        free(data);
        fclose(f);
        fprintf(stderr, "load error: cannot read %s\n", path);
        return 2;
    }
    fclose(f);

    if (bsm_validate(data, len, &img, err, sizeof(err)) != 0) {
        fprintf(stderr, "load error: %s\n", err);
        free(data);
        return 2;
    }
    ninstr = img.code_len / 8;

    if (disasm_only) {
        size_t k;
        for (k = 0; k < ninstr; k++) {
            char buf[128];
            uint64_t word = bsm_rd64le(img.code + k * 8);
            bsm_disasm(word, buf, sizeof(buf));
            printf("%06zu: %s\n", k, buf);
        }
        free(data);
        return 0;
    }

    bsm_regs_init(&regs);
    regs.ip = img.entry_index;
    bsm_mem_init(&mem);

    for (;;) {
        uint64_t word;
        uint64_t pc_before;
        uint8_t op, d, a, b;
        int32_t imm;
        int64_t simm;
        uint64_t next_ip;
        char dis[128];
        char eff[160];
        int halted = 0;
        const char *trap = NULL;
        int64_t trap_code = 0;

        if (executed >= budget) {
            fprintf(stderr, "error: instruction budget exceeded "
                            "after %" PRIu64 " instructions\n",
                    executed);
            bsm_mem_destroy(&mem);
            free(data);
            return 4;
        }
        if (regs.ip == ninstr) {
            halted = 1;
            if (trace) {
                printf("trace pc=%06" PRIu64 " halt\n", regs.ip);
            }
            break;
        }
        if (regs.ip > ninstr) {
            fprintf(stderr, "trap: out-of-code jump at pc=%" PRIu64
                            " after %" PRIu64 " instructions\n",
                    regs.ip, executed);
            bsm_mem_destroy(&mem);
            free(data);
            return 3;
        }
        pc_before = regs.ip;
        word = bsm_rd64le(img.code + (size_t)regs.ip * 8);
        op = bsm_op(word);
        d = bsm_dest(word);
        a = bsm_src_a(word);
        b = bsm_src_b(word);
        imm = bsm_imm32(word);
        simm = (int64_t)imm;
        next_ip = regs.ip + 1;
        eff[0] = '\0';
        bsm_disasm(word, dis, sizeof(dis));

        if (!bsm_op_implemented(op)) {
            trap = "illegal-opcode";
            trap_code = op;
            goto trap_out;
        }

        switch (op) {
        case BSM_OP_MOV:
            if (!reg_ok(d) || !reg_ok(a)) {
                trap = "invalid-register";
                goto trap_out;
            }
            regs.gpr[d] = regs.gpr[a] + (uint64_t)simm;
            snprintf(eff, sizeof(eff), "R%u=0x%016" PRIx64, d,
                     regs.gpr[d]);
            break;
        case BSM_OP_LOAD: {
            int64_t off;
            uint64_t value = 0;
            bsm_mem_err merr;
            if (!reg_ok(d) || !reg_ok(a) || !reg_ok(b)) {
                trap = "invalid-register";
                goto trap_out;
            }
            off = (int64_t)regs.gpr[b] + simm;
            if (off < 0) {
                trap = "out-of-bounds access";
                goto trap_out;
            }
            merr = bsm_mem_load64(&mem, regs.gpr[a], (uint64_t)off,
                                  &value);
            if (merr != BSM_MEM_OK) {
                trap = bsm_mem_err_name(merr);
                goto trap_out;
            }
            regs.gpr[d] = value;
            snprintf(eff, sizeof(eff), "R%u=0x%016" PRIx64, d, value);
            break;
        }
        case BSM_OP_STORE: {
            int64_t off;
            bsm_mem_err merr;
            if (!reg_ok(d) || !reg_ok(a) || !reg_ok(b)) {
                trap = "invalid-register";
                goto trap_out;
            }
            off = (int64_t)regs.gpr[b] + simm;
            if (off < 0) {
                trap = "out-of-bounds access";
                goto trap_out;
            }
            merr = bsm_mem_store64(&mem, regs.gpr[a], (uint64_t)off,
                                   regs.gpr[d]);
            if (merr != BSM_MEM_OK) {
                trap = bsm_mem_err_name(merr);
                goto trap_out;
            }
            snprintf(eff, sizeof(eff), "mem[h=%" PRIu64 " off=%" PRIu64
                                       "]=0x%016" PRIx64,
                     regs.gpr[a], (uint64_t)off, regs.gpr[d]);
            break;
        }
        case BSM_OP_ALLOC: {
            int64_t size;
            uint64_t handle = 0;
            bsm_mem_err merr;
            if (!reg_ok(d) || !reg_ok(a)) {
                trap = "invalid-register";
                goto trap_out;
            }
            size = (int64_t)regs.gpr[a] + simm;
            if (size < 0) {
                trap = "allocation too large";
                goto trap_out;
            }
            merr = bsm_mem_alloc(&mem, (uint64_t)size, &handle);
            if (merr != BSM_MEM_OK) {
                trap = bsm_mem_err_name(merr);
                goto trap_out;
            }
            regs.gpr[d] = handle;
            snprintf(eff, sizeof(eff), "R%u=handle %" PRIu64, d, handle);
            break;
        }
        case BSM_OP_FREE: {
            bsm_mem_err merr;
            if (!reg_ok(a)) {
                trap = "invalid-register";
                goto trap_out;
            }
            merr = bsm_mem_free(&mem, regs.gpr[a]);
            if (merr != BSM_MEM_OK) {
                trap = bsm_mem_err_name(merr);
                goto trap_out;
            }
            snprintf(eff, sizeof(eff), "freed handle %" PRIu64,
                     regs.gpr[a]);
            break;
        }
        case BSM_OP_JMP: {
            int64_t tgt;
            if (!reg_ok(a)) {
                trap = "invalid-register";
                goto trap_out;
            }
            tgt = (int64_t)regs.gpr[a] + simm;
            if (tgt < 0 || (uint64_t)tgt > ninstr) {
                trap = "out-of-code jump";
                goto trap_out;
            }
            next_ip = (uint64_t)tgt;
            snprintf(eff, sizeof(eff), "ip=%" PRIu64, next_ip);
            break;
        }
        case BSM_OP_BR: {
            int cond = d;
            int taken = 0;
            int64_t tgt;
            if (!reg_ok(a)) {
                trap = "invalid-register";
                goto trap_out;
            }
            switch (cond) {
            case 0: taken = regs.z; break;
            case 1: taken = !regs.z; break;
            case 2: taken = regs.n; break;
            case 3: taken = !regs.n; break;
            case 4: taken = regs.c; break;
            case 5: taken = !regs.c; break;
            case 6: taken = regs.v; break;
            case 7: taken = !regs.v; break;
            default:
                trap = "invalid-branch-condition";
                goto trap_out;
            }
            if (taken) {
                tgt = (int64_t)regs.gpr[a] + simm;
                if (tgt < 0 || (uint64_t)tgt > ninstr) {
                    trap = "out-of-code jump";
                    goto trap_out;
                }
                next_ip = (uint64_t)tgt;
            }
            snprintf(eff, sizeof(eff), "taken=%d ip=%" PRIu64, taken,
                     next_ip);
            break;
        }
        case BSM_OP_CALL: {
            int64_t tgt;
            if (!reg_ok(a)) {
                trap = "invalid-register";
                goto trap_out;
            }
            tgt = (int64_t)regs.gpr[a] + simm;
            if (tgt < 0 || (uint64_t)tgt > ninstr) {
                trap = "out-of-code jump";
                goto trap_out;
            }
            if (depth >= BSM_RETSTACK_SIZE) {
                trap = "return-stack overflow";
                goto trap_out;
            }
            retstack[depth++] = next_ip;
            regs.sp = depth;
            next_ip = (uint64_t)tgt;
            snprintf(eff, sizeof(eff), "ip=%" PRIu64 " sp=%" PRIu64,
                     next_ip, regs.sp);
            break;
        }
        case BSM_OP_RET:
            if (depth == 0) {
                halted = 1;
                snprintf(eff, sizeof(eff), "halt");
            } else {
                next_ip = retstack[--depth];
                regs.sp = depth;
                snprintf(eff, sizeof(eff), "ip=%" PRIu64 " sp=%" PRIu64,
                         next_ip, regs.sp);
            }
            break;
        case BSM_OP_TRAP:
            trap = "guest trap";
            trap_code = simm;
            goto trap_out;
        case BSM_OP_ADD:
        case BSM_OP_SUB:
        case BSM_OP_MUL:
        case BSM_OP_DIV:
        case BSM_OP_CMP:
        case BSM_OP_AND:
        case BSM_OP_OR:
        case BSM_OP_XOR:
        case BSM_OP_SHL:
        case BSM_OP_SHR:
        case BSM_OP_POPCNT: {
            uint64_t av, bv, result = 0;
            if (!reg_ok(a) || !reg_ok(b) ||
                (op != BSM_OP_CMP && !reg_ok(d))) {
                trap = "invalid-register";
                goto trap_out;
            }
            av = regs.gpr[a];
            bv = regs.gpr[b];
            if (op == BSM_OP_ADD) {
                result = av + bv;
                bsm_regs_set_flags_add(&regs, av, bv, result);
            } else if (op == BSM_OP_SUB) {
                result = av - bv;
                bsm_regs_set_flags_sub(&regs, av, bv, result);
            } else if (op == BSM_OP_MUL) {
                result = av * bv;
                bsm_regs_set_flags_logic(&regs, result);
            } else if (op == BSM_OP_DIV) {
                int64_t sa = (int64_t)av;
                int64_t sb = (int64_t)bv;
                if (sb == 0) {
                    trap = "division by zero";
                    goto trap_out;
                }
                if (sa == INT64_MIN && sb == -1) {
                    trap = "division overflow";
                    goto trap_out;
                }
                result = (uint64_t)(sa / sb);
                bsm_regs_set_flags_logic(&regs, result);
            } else if (op == BSM_OP_CMP) {
                result = av - bv;
                bsm_regs_set_flags_sub(&regs, av, bv, result);
                snprintf(eff, sizeof(eff),
                         "flags Z=%d N=%d C=%d V=%d", regs.z, regs.n,
                         regs.c, regs.v);
                break;
            } else if (op == BSM_OP_AND) {
                result = av & bv;
                bsm_regs_set_flags_logic(&regs, result);
            } else if (op == BSM_OP_OR) {
                result = av | bv;
                bsm_regs_set_flags_logic(&regs, result);
            } else if (op == BSM_OP_XOR) {
                result = av ^ bv;
                bsm_regs_set_flags_logic(&regs, result);
            } else if (op == BSM_OP_SHL) {
                result = av << (bv % 64);
                bsm_regs_set_flags_logic(&regs, result);
            } else if (op == BSM_OP_SHR) {
                result = av >> (bv % 64);
                bsm_regs_set_flags_logic(&regs, result);
            } else { /* POPCNT */
                result = popcount64(av);
                bsm_regs_set_flags_logic(&regs, result);
            }
            regs.gpr[d] = result;
            snprintf(eff, sizeof(eff),
                     "R%u=0x%016" PRIx64 " flags Z=%d N=%d C=%d V=%d", d,
                     result, regs.z, regs.n, regs.c, regs.v);
            break;
        }
        default:
            trap = "illegal-opcode";
            trap_code = op;
            goto trap_out;
        }

        regs.ip = next_ip;
        executed++;
        if (trace) {
            printf("trace pc=%06" PRIu64 " word=0x%016" PRIx64 " %s ; %s\n",
                   pc_before, word, dis, eff);
        }
        if (halted) {
            break;
        }
        continue;

    trap_out:
        if (trace) {
            printf("trace pc=%06" PRIu64 " word=0x%016" PRIx64 " %s ; "
                   "trap %s\n",
                   pc_before, word, dis, trap);
        }
        fprintf(stderr, "trap: %s", trap);
        if (trap_code != 0) {
            fprintf(stderr, " code=%" PRId64, trap_code);
        }
        fprintf(stderr, " at pc=%" PRIu64 " after %" PRIu64
                        " instructions\n",
                regs.ip, executed);
        bsm_mem_destroy(&mem);
        free(data);
        return 3;
    }

    printf("halt\n");
    printf("executed: %" PRIu64 "\n", executed);
    dump_regs(&regs);
    bsm_mem_destroy(&mem);
    free(data);
    return 0;
}
