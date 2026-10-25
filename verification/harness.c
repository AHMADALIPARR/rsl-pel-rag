/* BSM-64 verification harness.
 * Runs the built VM binary against fixtures as subprocesses
 * (fork/exec with a pipe for combined stdout+stderr) and asserts
 * exit statuses and output contents. This inspects real execution
 * results, never source text.
 * Usage: harness <path-to-bsm64> <fixtures-directory>
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

/* Run the VM with up to two extra arguments before the fixture path.
 * Returns the child exit code (or 128+signal), and *out holds the
 * malloc'd combined output, which the caller frees. */
static int run_vm(const char *file, const char *a1, const char *a2,
                  char **out) {
    char path[1024];
    int pipefd[2];
    pid_t pid;
    char *args[6];
    int nargs = 0;
    size_t cap = 8192;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    int status = 0;

    snprintf(path, sizeof(path), "%s/%s", g_fix, file);
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
        args[nargs++] = (char *)g_vm;
        if (a1 != NULL) {
            args[nargs++] = (char *)a1;
        }
        if (a2 != NULL) {
            args[nargs++] = (char *)a2;
        }
        args[nargs++] = path;
        args[nargs] = NULL;
        execv(g_vm, args);
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

int main(int argc, char **argv) {
    char *out = NULL;
    int code;
    double t0, t1, elapsed, ips;
    uint64_t executed = 0;
    const char *p;

    if (argc != 3) {
        fprintf(stderr, "usage: harness <bsm64> <fixtures-dir>\n");
        return 1;
    }
    g_vm = argv[1];
    g_fix = argv[2];

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

    printf("RESULT: %d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
