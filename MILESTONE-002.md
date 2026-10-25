# BSM-64 Milestone 002 — Build and Verification Report

## What this milestone is

The original assembler (Stage 2 of the CODEX MASTER DIRECTIVE) for
exactly the Milestone 001 opcode subset: a C11 tool that parses a
documented textual assembly grammar and emits validated PQRB
files, reusing the Milestone 001 decoder/validator and hand-written
SHA-256. No new opcodes, no language frontend, no SQL, region,
agent, or tensor stages. This report is the build instructions and
the final report for the milestone. It does not replace
MILESTONE-001.md, which is untouched, and it does not replace the
README, which is untouched.

Scope boundary, stated plainly: this work is additive relative to
the pre-BSM-64 repo. The existing Rust/Elixir RSL work (`rust/`,
`elixir/`, `sql/`, `docs/`, `examples/`), `README.md`, and
`LICENSE` are untouched (verified by `git diff` against the
Milestone 001 commit). BSM-64 files from Milestone 001 were
extended only where noted below: `Makefile`,
`verification/harness.c`, three short documentation amendments
(`constitution/instruction-set.spec` section 8,
`constitution/binary-abi.spec` section 5,
`bootstrap/stage0/NOTES.md` appendix), each marked as Milestone
002 content. No second loader and no second hash implementation
were written: the assembler calls `bsm_enc`, `bsm_validate`, and
`bsm_sha256` from Milestone 001.

## Files created and changed

```
constitution/assembly-grammar.spec   Grammar: lexical rules, per-
                                     opcode operand forms, labels,
                                     immediates, error catalog,
                                     exit statuses, emission
machine/assembler/bsmasm.c           The assembler (build/bsmasm):
                                     two-pass, label resolution,
                                     PQRB emission, self-validation
fixtures/arith.asm                   Hand-authored arith program
fixtures/loop.asm                    Hand-authored loop program
fixtures/callret.asm                 Hand-authored callret program
fixtures/memory.asm                  Hand-authored memory program
fixtures/asm-arith.pqrb              Assembled by make from arith.asm
fixtures/asm-loop.pqrb               Assembled by make from loop.asm
fixtures/asm-callret.pqrb            Assembled by make from callret.asm
fixtures/asm-memory.pqrb             Assembled by make from memory.asm
fixtures/asm-errors/*.asm            10 assembler error fixtures
                                     (see error catalog below)
Makefile                             Builds bsmasm, assembles the
                                     .asm fixtures, harness takes
                                     the bsmasm path
verification/harness.c               Assemble-then-run tests,
                                     disassembly round-trip test,
                                     digest-corruption test, 10
                                     assembler error tests
MILESTONE-002.md                     This file
```

The Milestone 001 generated fixtures (`fixtures/*.pqrb` from
`fixtures/gen.c`) are kept and still tested. The assembled
`asm-*.pqrb` outputs are byte-identical to the corresponding
generator outputs (verified with `cmp` for all four programs),
which is the strongest form of the claim that the assembler
reproduces the Milestone 001 programs: same words, same layout,
same digest.

## Grammar key decisions

Full grammar: `constitution/assembly-grammar.spec`. The decisions
that most affect what programs mean:

- **Syntax mirrors canonical disassembly.** Every disassembly line
  in instruction-set.spec section 7 is valid assembler input
  (except `ILLEGAL 0x..`, which is correctly rejected as an
  unknown opcode). `MOV R1, R0, 40`, `ADD R3, R1, R2`,
  `BR EQ, R0, 6`, `STORE R2, R1, R0, 0` all assemble as written.
  Commas are separators, like whitespace; mnemonics, registers,
  and condition codes match case-insensitively.
- **Operand syntax per opcode (DEST/SRC_A/SRC_B/IMM32):**
  MOV `D, A | D, A, I`; LOAD/STORE `D, A, B | D, A, B, I` (STORE's
  first operand is the VALUE register, matching the DEST field);
  ALLOC `D, A | D, A, I`; FREE `A`; JMP/CALL `A, I | I`;
  BR `C, A, I | C, I`; RET none; TRAP `I` only; ADD/SUB/MUL/DIV/
  AND/OR/XOR/SHL/SHR `D, A, B`; CMP `A, B`; POPCNT `D, A`.
  Omitted immediates emit 0. Any other count or class is a
  "wrong operand count" error naming the opcode and its forms.
- **Labels are absolute instruction indices.** A label's value is
  the 0-based index of the next instruction after its definition;
  `name:` may precede an instruction on the same line or stand
  alone, and several labels may share one instruction. Forward
  and backward references both work (two-pass assembly). A label
  in any immediate position places its index in IMM32. Jump
  targets are computed by the VM as R[SRC_A] + sext(IMM32), so a
  bare `JMP loop` (SRC_A = R0) lands on the label only while R0
  holds 0, its initial value; this contract is specified, not
  hidden behind relative-offset relocation, which the assembler
  deliberately does not perform.
- **Immediate ranges:** decimal or `0x` hex, optional leading
  `-`, must fit signed 32-bit (-2147483648..2147483647).
  Out-of-range digits are an "immediate out of range" error;
  malformed text (`0x` alone, `12x`, `+5`) is a "malformed
  immediate" error. Registers are `r0`..`r31`; anything else in
  register position is a "bad register" error.
- **Strictness beyond the loader:** later-family mnemonics are
  "unknown opcode" errors at assembly time even though the same
  byte values trap at run time in hand-built binaries. Directives
  (any `.`-prefixed token) are "unknown directive" errors: this
  milestone implements no directives, macros, data sections,
  multi-file assembly, or linking, and none are silently ignored.
- **Comments and lines:** `;` starts a comment to end of line;
  blank and comment-only lines are allowed; lines are capped at
  1024 bytes. A file with no instructions is an error ("no
  instructions"), because a CODE section must have nonzero
  length.

## Assembler features implemented

- Two-pass assembly with forward/backward label resolution,
  duplicate-label and undefined-label detection with line
  numbers.
- PQRB emission identical in layout to `fixtures/gen.c`: 64-byte
  header, one CODE section (EXEC|READ, offset 96, alignment 8),
  entry-point offset 96, SHA-256 digest of post-header bytes.
- Self-validation: the in-memory image is passed through
  `bsm_validate` before any file is written. Validation failure
  is exit status 3 (internal error) and writes nothing, because
  a correct assembler cannot produce an invalid image from a
  program it accepted.
- Precise errors, one per line, as
  `<input>:<line>: error: <message>`; the first error stops
  assembly and leaves any pre-existing output file untouched
  (asserted by the harness for every error fixture).
- Exit statuses: 0 assembled, 1 usage, 2 assembly error,
  3 host/internal error (cannot read input, cannot write output,
  out of memory, self-validation failure).

## Assembler error catalog (each covered by a fixture + test)

```
unknown-opcode.asm      unknown opcode
bad-register.asm        bad register
imm-out-of-range.asm    immediate out of range
imm-malformed.asm       malformed immediate
undefined-label.asm     undefined label
duplicate-label.asm     duplicate label
wrong-operand-count.asm wrong operand count
no-instructions.asm     no instructions (comment-only file)
unknown-directive.asm   unknown directive
unknown-condition.asm   unknown condition code
```

The "truncated/malformed input" class from the milestone brief is
covered by `no-instructions.asm` (a file with no assemblable
content) together with `wrong-operand-count.asm` (a line cut
short mid-operand list); both are assembly errors, exit 2.

## Bootstrap tools used (as invoked)

- `cc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`
- `GNU ld (GNU Binutils for Ubuntu) 2.42`
- `GNU Make 4.3`
- Flags: `-std=c11 -Wall -Wextra -O2` for every target.
- No external libraries, no Python, no Node.js/JavaScript/
  TypeScript anywhere in the new work. A marker grep over the
  new/changed tree returns no TODO/FIXME/stub placeholders; the
  only `XXXX` hits are the `mkdtemp` template string in the
  harness (required by that API) and the Milestone 001 report's
  own note about the deliberate bad-magic constant in
  `fixtures/gen.c`.

## Build and test commands run (from a clean tree)

```
make clean && make          # builds bsm64, bsmasm, gen,
                            # fixtures, asm fixtures, harness
make check                  # builds everything, runs the harness
```

The full `make clean && make` output contains zero warnings
(verified: `make 2>&1 | grep -cE "warning|error"` printed 0).

## Actual test results

Harness command: `./build/harness ./build/bsm64 fixtures ./build/bsmasm`

```
RESULT: 55 passed, 0 failed
```

Milestone 001 baseline: 34 passed, 0 failed. This milestone adds
21 tests, and none of the 34 were dropped or weakened:

- 4 Makefile-assembled fixture runs (`asm-arith`,
  `asm-loop-sum-55`, `asm-callret`, `asm-memory-load-store`)
  with the same register assertions as their Milestone 001
  counterparts.
- 4 assemble-then-run tests (fresh assembly from `.asm` source
  in the harness temp dir, then execution, same assertions).
- 1 disassembly round-trip test (assembler output disassembles
  canonically: `ADD R3, R1, R2`, `MUL R5, R3, R4`).
- 1 digest-corruption test: one code byte flipped post-assembly
  in a copy, and the loader rejects the copy with exit 2,
  proving the digest path covers assembler output.
- 10 assembler error tests (catalog above): exit 2, the stderr
  message names the error class, and no output file is written.
- 1 temp-dir setup check.

Additionally, outside the harness and before it was trusted: the
four assembled outputs were compared byte-for-byte with the
Milestone 001 generator outputs (`cmp`: identical for all four),
and every error fixture was run by hand against `build/bsmasm`
with the expected line-numbered message and exit 2, plus usage
with no arguments exiting 1.

## Binary sizes (measured, this machine only)

- `build/bsmasm`: 34512 bytes
- `build/bsm64`: 30400 bytes (unchanged from Milestone 001)
- `build/gen`: 34008 bytes (unchanged)
- `build/harness`: 25968 bytes (17152 bytes in Milestone 001)

## Known limitations (explicit)

- Assembler covers the Milestone 001 opcode subset only. No new
  opcodes, no directives, no macros, no conditional assembly, no
  data sections or string literals, no multi-file assembly or
  linking, no relative jumps, no floating-point.
- Still no language frontend, no SQL, no region algebra, no
  relational/retrieval/agent/storage/trust stages, no
  self-hosting, no guest syscalls or input (intentional
  determinism, unchanged from Milestone 001).
- Capability model remains minimal (Milestone 001 scope).
- The assembler emits exactly one CODE section with entry at
  instruction 0; programs needing data sections must wait for
  the directive/data milestone.
- Label values are absolute instruction indices; programs that
  clobber R0 must use an explicit zero-valued base register in
  bare-label jump operands (specified in the grammar, not a
  silent relocation).

## Next milestone (proposed)

Milestone 003: the PEL/PQ language frontend (Stage 3) — lexer,
parser, symbol resolution, static type checking, and typed IR
lowering to the Milestone 001 instruction subset, with the
assembler as the emission backend. No new opcode families until
the frontend for the current subset is verified end to end.
