# BSM-64 Milestone 001 — Build and Verification Report

## What this milestone is

The smallest verifiable executable of the BSM-64 Sovereign Binary
Semantic Machine, Path A (Original Virtual Machine): a C11 host
VM that loads, validates, and deterministically executes PQRB
programs implementing the Machine, Control, Arithmetic, and
Binary opcode families. This report is the build instructions and
the final report for the milestone. It does not replace the
README, which is untouched.

Scope boundary, stated plainly: the existing Rust/Elixir RSL work
in this repo (`rust/`, `elixir/`, `sql/`, `docs/`, `examples/`)
is prior art in the same repo. It is NOT a dependency of this
milestone, was NOT modified, and is NOT claimed as part of BSM-64.
All Milestone 001 work is additive, in new directories only, plus
this file and build-artifact ignore lines in `.gitignore`.

## Files created (nothing existing modified except `.gitignore`)

```
Makefile                          Top-level build (make + system cc)
MILESTONE-001.md                  This file
.gitignore                        Appended only: build/

constitution/binary-abi.spec      PQRB format, 64-byte header,
                                  section table, SHA-256 digest,
                                  loader rejection rules
constitution/instruction-set.spec Instruction word, opcode map,
                                  per-opcode semantics, flags,
                                  traps, exit statuses, disassembler
constitution/memory-model.spec    Handle memory, bounds checks,
                                  ownership, violation taxonomy
constitution/capability-model.spec Section capability mask,
                                  loader + runtime enforcement,
                                  tests proving enforcement

bootstrap/stage0/NOTES.md         Host VM identity, machine state,
                                  no guest syscalls in M001, ELF
                                  notes, Stage 2 deferrals

machine/bsm_sha256.h, machine/bsm_sha256.c
                                  Hand-written SHA-256 (FIPS 180-4)
machine/decoder/bsm_decoder.h, machine/decoder/bsm_decoder.c
                                  Format decoder, validator,
                                  canonical disassembler, encoder
machine/registers/bsm_registers.h, machine/registers/bsm_registers.c
                                  32 x 64-bit GPRs + IP + SP +
                                  flags Z, N, C, V
machine/memory/bsm_memory.h, machine/memory/bsm_memory.c
                                  Bounds-checked handle memory
machine/vm.c                      The bsm64 VM: loader, execution
                                  loop, --trace, --disasm,
                                  --max-instructions

fixtures/gen.c                    Fixture generator (C, validated
                                  output, deliberate malformed mode)
fixtures/*.pqrb                   32 generated fixture binaries
                                  (see fixture list below)

verification/harness.c            Execution harness (fork/exec,
                                  exit-status and output asserts)
```

## Bootstrap tools used (as invoked)

- `cc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`
- `GNU ld (GNU Binutils for Ubuntu) 2.42`
- `GNU Make 4.3`
- Host: `Linux htch-runtime 7.0.0-39-generic x86_64 GNU/Linux`
- Flags: `-std=c11 -Wall -Wextra -O2` for every target.
- No external libraries, no Python, no Node.js/JavaScript/
  TypeScript anywhere in the new work. Linux kernel + x86-64 +
  system cc/ld are the only bootstrap boundaries.

## Implemented opcodes (exact list)

Machine: MOV (0x01), LOAD (0x02), STORE (0x03), ALLOC (0x04),
FREE (0x05).
Control: JMP (0x10), BR (0x11), CALL (0x12), RET (0x13),
TRAP (0x14).
Arithmetic: ADD (0x20), SUB (0x21), MUL (0x22), DIV (0x23),
CMP (0x24).
Binary: AND (0x30), OR (0x31), XOR (0x32), SHL (0x33), SHR (0x34),
POPCNT (0x35).

Reserved ranges, decoder traps on all of them: Region
0x40-0x4F, Relational 0x50-0x5F, Retrieval 0x60-0x6F, Agent
0x70-0x7F, Storage 0x80-0x8F, Trust 0x90-0x9F; 0x00 and 0xA0-0xFF
are also unassigned and trap identically.

Key semantic choices (all specified in
`constitution/instruction-set.spec` and tested):
- 64-bit wraparound arithmetic (mod 2^64).
- DIV is signed; division by zero traps; INT64_MIN / -1 traps
  (division overflow) rather than wrapping silently.
- Shifts are logical, count = R[SRC_B] mod 64; edge cases 0, 63,
  64, 65 are all tested (64 leaves the value unchanged, 65
  shifts by 1).
- CMP sets Z, N, C, V; BR tests them with condition codes
  EQ/NE/MI/PL/CS/CC/VS/VC.
- MOV is DEST = R[SRC_A] + sign-extended IMM32, so immediates
  load through a zero register and register copies use IMM32 0.
- Instruction word: OPCODE:8 | DEST:8 | SRC_A:8 | SRC_B:8 |
  IMM32:32, stored little-endian.

## Fixtures generated (by `fixtures/gen.c`, reproducible via make)

Valid execution programs: arith, loop (sum 1..10 = 55), callret
(21 doubled = 42), memory (round-trip of 0x1122334455667788),
bench (200000-iteration loop, 600004 instructions).
Boundary programs: wrap, divzero, divovf, shift0, shift63,
shift64, shift65, popcnt, branchtaken, branchnottaken, oobload,
oobstore, unallochandle, doublefree, useafterfree, jumpoutside,
trap, infinite (budget test).
Malformed programs (deliberate, each rejected by the validator
except the last, which is format-valid and traps at run time):
badmagic, badversion, truncated, sectionoob, entryoutside,
digestmismatch, overlapping, badalignment, illegalopcode
(opcode 0x40, Region range).
Re-running the generator produces byte-identical files
(verified by sha256sum before and after regeneration: no diff).

## Build and test commands run (from a clean tree)

```
make clean && make          # builds bsm64, gen, fixtures, harness
make check                  # builds everything, runs the harness
```

The full `make clean && make` output contains zero warnings
(verified: `make 2>&1 | grep -cE "warning|error"` printed 0).
A marker grep over the new tree (placeholder-marker pattern
search) returns exactly one hit:
the string `"XXXX"` in `fixtures/gen.c`, which is the deliberate
bad-magic constant for the badmagic fixture, not a placeholder.
No placeholder code ships in this milestone.

SHA-256 was verified independently against FIPS 180-4 test
vectors with a throwaway test program (not shipped): empty input
-> e3b0c442..., "abc" -> ba7816bf..., and the 56-byte "abcdbc..."
vector -> 248d6a61...; all three matched exactly.

## Actual test results

Harness command: `./build/harness ./build/bsm64 fixtures`

```
RESULT: 34 passed, 0 failed
```

The 34 tests span: 4 valid execution programs; 8 malformed
load rejections (exit 2); illegal-opcode trap (exit 3);
arithmetic boundaries (wraparound, DIV by zero trap, DIV
overflow trap, shifts 0/63/64/65, POPCNT 4 and 64); branch
taken and not-taken; CALL/RET; memory violations
(out-of-bounds load, out-of-bounds store, unallocated handle,
double-free, use-after-free); jump-outside-code trap; guest
TRAP; trace determinism (two --trace runs byte-identical, and
containing the ADD disassembly); instruction-budget exceeded
(exit 4 with --max-instructions 1000 on the infinite loop);
--disasm mode; and the benchmark below.

## Built VM binary size

- `build/bsm64`: 30400 bytes
- `build/gen`: 34008 bytes
- `build/harness`: 17152 bytes

## Real performance measurement (this machine only)

Loop benchmark fixture (`bench.pqrb`, 600004 instructions),
timed around a real subprocess run by the harness:

```
BENCH executed=600004 seconds=0.233220 ips=2572701
```

That is approximately 2.57 million instructions per second on
this host (Linux htch-runtime 7.0.0-39-generic x86_64, cc 13.3.0,
-O2). An earlier run on the same host measured 2,820,518 ips for
the same fixture, so treat the figure as a single-machine,
single-run observation with ordinary run-to-run variance, never
as a general benchmark. The instruction count (600004) is exact
and asserted by the harness; only the wall-clock rate varies.

## Trace mode

`bsm64 --trace <file>` prints one deterministic line per
executed instruction: instruction index, raw 64-bit word,
canonical disassembly, and the resulting register/flag effect,
for example:

```
trace pc=000002 word=0x2003010200000000 ADD R3, R1, R2 ; R3=0x000000000000002a flags Z=0 N=0 C=0 V=0
```

Two runs of the same input produce byte-identical trace output
(asserted by the harness). On normal halt the VM prints
`executed: N`, the flags, and all 32 registers. Exit statuses:
0 halt, 1 usage, 2 load/validation error, 3 guest trap,
4 budget exceeded, 5 host internal error.

## Known limitations (explicit)

- No language frontend yet; no assembler yet; guest programs
  are produced by the C fixture generator, and hand-authored
  x86-64 guest code plus the original assembler are Stage 2
  work (see `bootstrap/stage0/NOTES.md`).
- No SQL, no region algebra, no relational/retrieval/agent/
  storage/trust stages: those opcode ranges are reserved and
  trap.
- No self-hosting: no part of the toolchain is written in the
  guest instruction set.
- Capability model is minimal: the section EXEC mask enforced
  by the loader and by fetch bounds, plus handle possession as
  the memory capability. Per-handle read/write bits, capability
  delegation, and DATA-section runtime enforcement are future
  work (see `constitution/capability-model.spec` section 6).
- Guest programs have no input, no syscalls, no files, no clock,
  and no host observability in this milestone; that isolation is
  intentional and is what makes execution deterministic.
- Memory limits: 1024 handles per run, 16 MiB per allocation.
- The VM's return stack is internal (1024 deep) and is not
  guest memory; there is no guest-visible stack in memory yet.

## State left for the parent

Work is UNCOMMITTED in the working tree, as instructed: no
commit, no push, no remote touched, no credentials used. The
only modified existing file is `.gitignore` (appended `build/`
ignore lines); `README.md`, `LICENSE`, `rust/`, `elixir/`,
`sql/`, `docs/`, and `examples/` are untouched (verified by
`git status`: one modified file, all else new). Build outputs
live in `build/` and are git-ignored.

## Independent verification (parent agent, before commit)

Re-ran from the uncommitted tree: `make clean && make`
(0 warnings/errors), `make check` →
`RESULT: 34 passed, 0 failed`. Spot-checked: `--trace`
output byte-identical across two runs of `fixtures/arith.pqrb`;
`fixtures/badmagic.pqrb` rejected with exit 2
(`load error: bad magic`); bench fixture on this run:
`BENCH executed=600004 seconds=0.237328 ips=2528166`
(same-machine variance vs the 2,572,701 figure above, as
noted there). SHA-256 re-verified independently against FIPS
180-4 vectors for empty input and "abc" (both exact).
Existing dirs confirmed untouched (`git diff` empty for
`README.md`, `LICENSE`, `rust/`, `elixir/`, `sql/`, `docs/`,
`examples/`); only `.gitignore` modified (+2 build lines).

## Next milestone (proposed)

Milestone 002: the original assembler — a C11 tool that parses
a small assembly syntax for exactly the M001 opcode subset and
emits validated PQRB files (reusing the decoder/validator and
SHA-256 code), plus hand-authored assembly fixtures replacing
the generator's hardcoded word streams for at least the arith,
loop, callret, and memory programs, with the harness extended to
assemble-then-run. No new opcode families until the assembler
for the M001 subset is verified end to end.
