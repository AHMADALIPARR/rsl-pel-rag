# Bootstrap Stage 0 notes (Milestone 001)

## What the M001 host VM is

The Milestone 001 host VM is a C11 program built with the system
C compiler and linker (cc/ld) on Linux x86-64. That is the whole
bootstrap boundary for this milestone: Linux kernel + x86-64 +
system cc/ld. No external libraries, no frameworks, no SQLite, no
vector databases, no Python, no Node.js/JavaScript/TypeScript
anywhere in the new work — build system is `make` plus the system
compiler only.

## Machine state

Guest machine state is exactly: 32 x 64-bit general-purpose
registers, an instruction pointer (instruction index within the
code section), a stack pointer (depth of the VM return stack),
and explicit condition flags Z, N, C, V. See
`constitution/instruction-set.spec` for flag semantics and
`machine/registers/` for the representation. Guest memory is
handle-addressed only; see `constitution/memory-model.spec`.

## Direct syscall interface

There is no guest-to-host syscall interface in Milestone 001.
The VM process itself uses only ordinary host library calls
(file reading, memory allocation, process exit) to load and run a
guest program; guest instructions cannot invoke host syscalls,
cannot open files, and cannot observe the host. A direct syscall
interface for guest programs, if one is ever added, is later
milestone work and is not claimed here.

## Minimal ELF notes

The host VM binary produced by the build is an ordinary ELF
executable produced by the system linker from the C11 sources in
`machine/`; there is no hand-written ELF emitter in this
milestone. Guest programs are PQRB files (see
`constitution/binary-abi.spec`), not ELF files, and the VM loads
them with its own validator, not with the host dynamic loader.

## Explicitly NOT claimed in this milestone

- Hand-authored x86-64 guest code: guest programs in this
  milestone are PQRB instruction-word streams produced by the C
  fixture generator in `fixtures/gen.c`. Writing guest code by
  hand in x86-64 assembly is Stage 2 work.
- The original assembler: there is no assembler program in this
  milestone. The fixture generator encodes instruction words
  directly from C constants; it is a test fixture tool, not an
  assembler product.
- Self-hosting: the VM does not run on itself, and no part of
  the toolchain is written in the guest instruction set yet.

Stage 0 is therefore deliberately small: a host VM that loads,
validates, and deterministically executes PQRB programs, with the
assembler and hand-authored guest code deferred to Stage 2 and
documented as such rather than implied.

## Milestone 002 update (Stage 2, assembler only)

The original assembler now exists: `machine/assembler/bsmasm.c`
(builds `build/bsmasm`), specified in
`constitution/assembly-grammar.spec`. It assembles the Milestone
001 opcode subset only, emits PQRB files, and validates its own
output through the loader before writing it. Hand-authored
assembly fixtures live in `fixtures/*.asm`. Hand-authored x86-64
guest code (machine code written directly, as opposed to BSM-64
assembly source) and self-hosting remain later work and are still
not claimed.
