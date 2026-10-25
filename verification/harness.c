/* BSM-64 verification harness.
 * Runs the built VM binary against fixtures as subprocesses
 * (fork/exec with a pipe for combined stdout+stderr) and asserts
 * exit statuses and output contents. This inspects real execution
 * results, never source text. Milestone 002 adds assembler tests:
 * it runs the built bsmasm binary on .asm fixtures (assemble-then-
 * run), on .asm error fixtures (asserting the error message), and
 * on a deliberately corrupted assembled file (asserting the loader
 * rejects it with a digest error).
 * Usage: harness <path-to-bsm64> <fixtures-directory> <path-to-bsmasm>
 * Prints one PASS/FAIL line per test, a BENCH line for the loop
 * benchmark, and a final RESULT line. Exits nonzero on any failure.
 */
#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const char *g_vm;
static const char *g_fix;
static const char *g_asm;
static int g_passed = 0;
static int g_failed = 0;

static void check(const char *name, int ok) {
    if (ok) {
        g_passed++;
        printf("PASS %s\n", name);
    } else {
        g_failed++;
        printf("FAIL %s\n", name);
    }
}

static int contains(const char *hay, const char *needle) {
    return hay != NULL && strstr(hay, needle) != NULL;
}

/* Run a program with an argument vector, capturing combined
 * stdout+stderr. Returns the child exit code (or 128+signal), and
 * *out holds the malloc'd combined output, which the caller frees. */
static int run_cmd(char *const *args, char **out) {
    int pipefd[2];
    pid_t pid;
    size_t cap = 8192;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    int status = 0;

    if (buf == NULL) {
        *out = NULL;
        return -1;
    }
    if (pipe(pipefd) != 0) {
        free(buf);
        *out = NULL;
        return -1;
    }
    pid = fork();
    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execv(args[0], args);
        _exit(127);
    }
    close(pipefd[1]);
    for (;;) {
        ssize_t r;
        if (len + 4096 > cap) {
            char *nb;
            cap *= 2;
            nb = (char *)realloc(buf, cap);
            if (nb == NULL) {
                break;
            }
            buf = nb;
        }
        r = read(pipefd[0], buf + len, cap - len - 1);
        if (r <= 0) {
            break;
        }
        len += (size_t)r;
    }
    close(pipefd[0]);
    buf[len] = '\0';
    while (waitpid(pid, &status, 0) < 0) {
    }
    *out = buf;
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return -1;
}

/* Run the VM with up to two extra arguments before the fixture path
 * (an absolute path). See run_vm for the fixtures-dir form. */
static int run_vm_path(const char *path, const char *a1, const char *a2,
                       char **out) {
    char *args[6];
    int nargs = 0;
    args[nargs++] = (char *)g_vm;
    if (a1 != NULL) {
        args[nargs++] = (char *)a1;
    }
    if (a2 != NULL) {
        args[nargs++] = (char *)a2;
    }
    args[nargs++] = (char *)path;
    args[nargs] = NULL;
    return run_cmd(args, out);
}

/* Run the VM with up to two extra arguments before the fixture path
 * (a name inside the fixtures directory). */
static int run_vm(const char *file, const char *a1, const char *a2,
                  char **out) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", g_fix, file);
    return run_vm_path(path, a1, a2, out);
}

/* Run the assembler: bsmasm <input.asm> <output.pqrb>. */
static int run_asm(const char *input, const char *output, char **out) {
    char *args[4];
    args[0] = (char *)g_asm;
    args[1] = (char *)input;
    args[2] = (char *)output;
    args[3] = NULL;
    return run_cmd(args, out);
}

static void test_exit0_regs(const char *name, const char *file,
                            const char *need1, const char *need2) {
    char *out = NULL;
    int code = run_vm(file, NULL, NULL, &out);
    int ok = (code == 0);
    if (need1 != NULL) {
        ok = ok && contains(out, need1);
    }
    if (need2 != NULL) {
        ok = ok && contains(out, need2);
    }
    if (!ok) {
        printf("  %s: exit=%d output:\n%s\n", name, code,
               out == NULL ? "(none)" : out);
    }
    check(name, ok);
    free(out);
}

static void test_exit(const char *name, const char *file, int want,
                      const char *a1, const char *a2) {
    char *out = NULL;
    int code = run_vm(file, a1, a2, &out);
    int ok = (code == want);
    if (!ok) {
        printf("  %s: exit=%d want=%d output:\n%s\n", name, code, want,
               out == NULL ? "(none)" : out);
    }
    check(name, ok);
    free(out);
}

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* Read a whole file into malloc'd memory; returns NULL on failure
 * and sets *len_out. Used by the digest-corruption test. */
static unsigned char *read_file(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    size_t len;
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    {
        long end = ftell(f);
        if (end < 0) {
            fclose(f);
            return NULL;
        }
        len = (size_t)end;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (unsigned char *)malloc(len == 0 ? 1 : len);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    if (len > 0 && fread(buf, 1, len, f) != len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len_out = len;
    return buf;
}

static int write_file(const char *path, const unsigned char *buf,
                      size_t len) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return 0;
    }
    if (len > 0 && fwrite(buf, 1, len, f) != len) {
        fclose(f);
        return 0;
    }
    if (fclose(f) != 0) {
        return 0;
    }
    return 1;
}

/* Assemble-then-run: assemble an .asm fixture to the temp dir, run
 * it, and assert exit 0 plus the expected register contents. */
static void test_asm_run(const char *name, const char *asmfile,
                         const char *tmpdir, const char *need1,
                         const char *need2) {
    char inpath[1024];
    char outpath[1024];
    char *out = NULL;
    char *out2 = NULL;
    int code;
    int ok;
    snprintf(inpath, sizeof(inpath), "%s/%s", g_fix, asmfile);
    snprintf(outpath, sizeof(outpath), "%s/%s", tmpdir, "run.pqrb");
    code = run_asm(inpath, outpath, &out);
    ok = (code == 0);
    free(out);
    out = NULL;
    if (ok) {
        code = run_vm_path(outpath, NULL, NULL, &out2);
        ok = (code == 0);
        if (need1 != NULL) {
            ok = ok && contains(out2, need1);
        }
        if (need2 != NULL) {
            ok = ok && contains(out2, need2);
        }
        if (!ok) {
            printf("  %s: vm exit=%d output:\n%s\n", name, code,
                   out2 == NULL ? "(none)" : out2);
        }
    } else {
        printf("  %s: assembler exit=%d\n", name, code);
    }
    check(name, ok);
    free(out2);
    unlink(outpath);
}

int main(int argc, char **argv) {
    char *out = NULL;
    int code;
    double t0, t1, elapsed, ips;
    uint64_t executed = 0;
    const char *p;

    if (argc != 4) {
        fprintf(stderr,
                "usage: harness <bsm64> <fixtures-dir> <bsmasm>\n");
        return 1;
    }
    g_vm = argv[1];
    g_fix = argv[2];
    g_asm = argv[3];

    test_exit0_regs("arith", "arith.pqrb", "R3=0x000000000000002a",
                    "R6=0x000000000000017a");
    test_exit0_regs("loop-sum-55", "loop.pqrb",
                    "R1=0x0000000000000037", NULL);
    test_exit0_regs("callret", "callret.pqrb",
                    "R2=0x000000000000002a",
                    "R3=0x000000000000002a");
    test_exit0_regs("memory-load-store", "memory.pqrb",
                    "R3=0x1122334455667788", NULL);

    test_exit("malformed-badmagic", "badmagic.pqrb", 2, NULL, NULL);
    test_exit("malformed-badversion", "badversion.pqrb", 2, NULL, NULL);
    test_exit("malformed-truncated", "truncated.pqrb", 2, NULL, NULL);
    test_exit("malformed-sectionoob", "sectionoob.pqrb", 2, NULL, NULL);
    test_exit("malformed-entryoutside", "entryoutside.pqrb", 2, NULL,
              NULL);
    test_exit("malformed-digestmismatch", "digestmismatch.pqrb", 2, NULL,
              NULL);
    test_exit("malformed-overlapping", "overlapping.pqrb", 2, NULL,
              NULL);
    test_exit("malformed-badalignment", "badalignment.pqrb", 2, NULL,
              NULL);
    test_exit("trap-illegal-opcode", "illegalopcode.pqrb", 3, NULL,
              NULL);

    test_exit0_regs("wrap-int64max-plus-1", "wrap.pqrb",
                    "R3=0x8000000000000000", NULL);
    test_exit("trap-div-by-zero", "divzero.pqrb", 3, NULL, NULL);
    test_exit("trap-div-overflow", "divovf.pqrb", 3, NULL, NULL);
    test_exit0_regs("shift-0", "shift0.pqrb",
                    "R3=0x0000000000001234", NULL);
    test_exit0_regs("shift-63", "shift63.pqrb",
                    "R3=0x8000000000000000", NULL);
    test_exit0_regs("shift-64-mod-64", "shift64.pqrb",
                    "R3=0x0000000000001234", NULL);
    test_exit0_regs("shift-65-mod-64", "shift65.pqrb",
                    "R3=0x0000000000002468", NULL);
    test_exit0_regs("popcnt", "popcnt.pqrb",
                    "R2=0x0000000000000004",
                    "R4=0x0000000000000040");
    test_exit0_regs("branch-taken", "branchtaken.pqrb",
                    "R3=0x0000000000000007", NULL);
    test_exit0_regs("branch-not-taken", "branchnottaken.pqrb",
                    "R3=0x0000000000000009", NULL);
    test_exit("trap-oob-load", "oobload.pqrb", 3, NULL, NULL);
    test_exit("trap-oob-store", "oobstore.pqrb", 3, NULL, NULL);
    test_exit("trap-unallocated-handle", "unallochandle.pqrb", 3, NULL,
              NULL);
    test_exit("trap-double-free", "doublefree.pqrb", 3, NULL, NULL);
    test_exit("trap-use-after-free", "useafterfree.pqrb", 3, NULL,
              NULL);
    test_exit("trap-jump-outside-code", "jumpoutside.pqrb", 3, NULL,
              NULL);
    test_exit("trap-guest-trap", "trap.pqrb", 3, NULL, NULL);

    /* Trace determinism: two runs must be byte-identical. */
    {
        char *out2 = NULL;
        int code2;
        code = run_vm("arith.pqrb", "--trace", NULL, &out);
        code2 = run_vm("arith.pqrb", "--trace", NULL, &out2);
        check("trace-deterministic",
              code == 0 && code2 == 0 && out != NULL && out2 != NULL &&
                  strcmp(out, out2) == 0 &&
                  contains(out, "ADD R3, R1, R2"));
        free(out);
        free(out2);
        out = NULL;
    }

    test_exit("budget-exceeded", "infinite.pqrb", 4,
              "--max-instructions", "1000");

    /* Disassembler mode. */
    code = run_vm("arith.pqrb", "--disasm", NULL, &out);
    check("disasm", code == 0 && contains(out, "ADD R3, R1, R2") &&
                       contains(out, "MUL R5, R3, R4"));
    free(out);
    out = NULL;

    /* Benchmark: loop fixture, timed around a real subprocess run. */
    t0 = now_seconds();
    code = run_vm("bench.pqrb", NULL, NULL, &out);
    t1 = now_seconds();
    elapsed = t1 - t0;
    p = out == NULL ? NULL : strstr(out, "executed: ");
    if (p != NULL) {
        executed = strtoull(p + 10, NULL, 10);
    }
    ips = elapsed > 0 ? (double)executed / elapsed : 0;
    printf("BENCH executed=%" PRIu64 " seconds=%.6f ips=%.0f\n",
           executed, elapsed, ips);
    check("bench", code == 0 && executed == 600004 && ips > 0);
    free(out);
    out = NULL;

    /* ---- Milestone 002: assembler tests ---- */

    /* Makefile-assembled .asm fixtures run with the same expected
     * state as the Milestone 001 fixtures they mirror. */
    test_exit0_regs("asm-arith", "asm-arith.pqrb",
                    "R3=0x000000000000002a",
                    "R6=0x000000000000017a");
    test_exit0_regs("asm-loop-sum-55", "asm-loop.pqrb",
                    "R1=0x0000000000000037", NULL);
    test_exit0_regs("asm-callret", "asm-callret.pqrb",
                    "R2=0x000000000000002a",
                    "R3=0x000000000000002a");
    test_exit0_regs("asm-memory-load-store", "asm-memory.pqrb",
                    "R3=0x1122334455667788", NULL);

    {
        char tmpl[] = "/tmp/bsm-asm-XXXXXX";
        char *tmpdir = mkdtemp(tmpl);
        size_t i;
        static const struct {
            const char *file;
            const char *needle;
        } asm_errors[] = {
            {"unknown-opcode.asm", "unknown opcode"},
            {"bad-register.asm", "bad register"},
            {"imm-out-of-range.asm", "immediate out of range"},
            {"imm-malformed.asm", "malformed immediate"},
            {"undefined-label.asm", "undefined label"},
            {"duplicate-label.asm", "duplicate label"},
            {"wrong-operand-count.asm", "wrong operand count"},
            {"no-instructions.asm", "no instructions"},
            {"unknown-directive.asm", "unknown directive"},
            {"unknown-condition.asm", "unknown condition code"},
        };
        check("asm-tmpdir", tmpdir != NULL);
        if (tmpdir != NULL) {
            /* Assemble-then-run, fresh from source each time. */
            test_asm_run("asm-run-arith", "arith.asm", tmpdir,
                         "R3=0x000000000000002a",
                         "R6=0x000000000000017a");
            test_asm_run("asm-run-loop", "loop.asm", tmpdir,
                         "R1=0x0000000000000037", NULL);
            test_asm_run("asm-run-callret", "callret.asm", tmpdir,
                         "R2=0x000000000000002a",
                         "R3=0x000000000000002a");
            test_asm_run("asm-run-memory", "memory.asm", tmpdir,
                         "R3=0x1122334455667788", NULL);

            /* Disassembly of assembler output is canonical. */
            code = run_vm("asm-arith.pqrb", "--disasm", NULL, &out);
            check("asm-disasm",
                  code == 0 && contains(out, "ADD R3, R1, R2") &&
                      contains(out, "MUL R5, R3, R4"));
            free(out);
            out = NULL;

            /* Digest path covers assembler output: assemble, flip
             * one code byte in a copy, and the loader must reject
             * it with a load error (exit 2). */
            {
                char inpath[1024];
                char goodpath[1024];
                char badpath[1024];
                unsigned char *bytes = NULL;
                size_t blen = 0;
                int ok = 0;
                snprintf(inpath, sizeof(inpath), "%s/arith.asm",
                         g_fix);
                snprintf(goodpath, sizeof(goodpath), "%s/good.pqrb",
                         tmpdir);
                snprintf(badpath, sizeof(badpath), "%s/bad.pqrb",
                         tmpdir);
                code = run_asm(inpath, goodpath, &out);
                free(out);
                out = NULL;
                if (code == 0) {
                    bytes = read_file(goodpath, &blen);
                }
                if (bytes != NULL && blen > 100) {
                    bytes[100] ^= 0xff;
                    if (write_file(badpath, bytes, blen)) {
                        code = run_vm_path(badpath, NULL, NULL, &out);
                        ok = (code == 2);
                        if (!ok) {
                            printf("  asm-digest-corrupt: exit=%d "
                                   "want=2 output:\n%s\n",
                                   code, out == NULL ? "(none)" : out);
                        }
                        free(out);
                        out = NULL;
                    }
                    free(bytes);
                }
                check("asm-digest-corrupt", ok);
                unlink(goodpath);
                unlink(badpath);
            }

            /* Assembler error classes: nonzero exit, the error
             * names the class, and no output file is written. */
            for (i = 0;
                 i < sizeof(asm_errors) / sizeof(asm_errors[0]); i++) {
                char inpath[1024];
                char outpath[1024];
                char name[128];
                int ok;
                snprintf(inpath, sizeof(inpath), "%s/asm-errors/%s",
                         g_fix, asm_errors[i].file);
                snprintf(outpath, sizeof(outpath), "%s/err.pqrb",
                         tmpdir);
                unlink(outpath);
                code = run_asm(inpath, outpath, &out);
                ok = (code == 2) &&
                     contains(out, asm_errors[i].needle) &&
                     access(outpath, F_OK) != 0;
                if (!ok) {
                    printf("  asm-err-%s: exit=%d output:\n%s\n",
                           asm_errors[i].file, code,
                           out == NULL ? "(none)" : out);
                }
                snprintf(name, sizeof(name), "asm-err-%s",
                         asm_errors[i].file);
                check(name, ok);
                free(out);
                out = NULL;
                unlink(outpath);
            }
            rmdir(tmpdir);
        }
    }

    printf("RESULT: %d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
