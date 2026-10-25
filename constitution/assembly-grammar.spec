BSM-64 Assembly Grammar Specification (Milestone 002)
======================================================
Status: implemented and verified by execution in this milestone.
The assembler is machine/assembler/bsmasm.c; it emits exactly the
PQRB format defined in constitution/binary-abi.spec, using the
instruction word defined in constitution/instruction-set.spec.
This document is the grammar; it does not change either spec.
Where this document and instruction-set.spec appear to disagree,
instruction-set.spec governs semantics and this document governs
surface syntax only.

1. Source file structure
------------------------
A source file is UTF-8 text, processed line by line. A line is at
most 1024 bytes including its newline; a longer line is an
assembler error ("line too long"). Line numbers in error messages
are physical 1-based line numbers of the source file, counting
blank and comment-only lines.

A line has this shape:

  [label:]...  [instruction]  [; comment]

- Leading whitespace is ignored.
- A trailing comment starts at the first ';' and runs to end of
  line. ';' inside a token never occurs, because no token contains
  ';': a ';' always starts a comment.
- A line with no instruction (blank, or comment-only, or labels
  only) defines labels but emits no instruction word.
- A file that defines no instruction at all is an error
  ("no instructions"): a PQRB CODE section must have nonzero
  length (binary-abi.spec section 3), so an empty program has no
  valid emission.

2. Lexical rules
----------------
After comment stripping and label extraction (section 4), the
remainder of a line is split into tokens. Commas are separators,
exactly like whitespace: "MOV R1, R0, 40" and "MOV R1 R0 40" are
the same token stream. A token is a maximal run of characters
that are not whitespace and not commas.

Token classes:

- Opcode mnemonic: one of the M001 opcode names (section 5),
  matched case-insensitively ("mov", "Mov", "MOV" are identical).
  Any other alphabetic token in opcode position is an
  "unknown opcode" error. An opcode name used as a label is an
  error at label definition time ("label uses a reserved name").
- Register: 'r' or 'R' followed by decimal digits, value 0..31.
  "R32", "R99", "R", "R1x" are "bad register" errors. Register
  tokens are matched case-insensitively for the 'r'.
- Condition code (BR only): EQ, NE, MI, PL, CS, CC, VS, VC,
  matched case-insensitively, with the meanings in
  instruction-set.spec section 3. Any other token in condition
  position is an "unknown condition code" error.
- Immediate: an optional '-' followed by either decimal digits
  or "0x"/"0X" followed by hex digits. The value must fit a
  signed 32-bit integer (-2147483648..2147483647). Violations:
  no digits ("0x" alone, "-" alone, "12x") are "malformed
  immediate" errors; digits that parse but exceed the signed
  32-bit range ("2147483648", "-2147483649", "0x80000000") are
  "immediate out of range" errors. A leading '+' is not accepted
  and is a malformed-immediate error.
- Label reference: an identifier [A-Za-z_][A-Za-z0-9_]*, at most
  128 characters, usable wherever an immediate is accepted
  (section 4). A longer identifier is an error ("identifier too
  long").
- Label definition: an identifier followed immediately by ':'
  (section 4).
- Directive: any token beginning with '.' in any position is an
  "unknown directive" error. Milestone 002 implements no
  directives: no data sections, no origin setting, no macros, no
  include. A later milestone may add directives; until then they
  are errors, never silently ignored.

3. Instruction forms (per opcode)
----------------------------------
The table gives the accepted operand sequences. D = destination
register, A = first source register, B = second source register,
I = immediate, L = label reference, C = condition code. Forms
separated by '|' are alternatives; any other operand count or
operand class is a "wrong operand count" error (the message names
the opcode and the expected forms).

  MOV    D, A          ; copy: DEST = R[A] + 0
         D, A, I       ; DEST = R[A] + sext(I)
         D, A, L       ; DEST = R[A] + label index
  LOAD   D, A, B       ; DEST = mem[handle R[A], R[B] + 0]
         D, A, B, I
         D, A, B, L
  STORE  D, A, B       ; mem[handle R[A], R[B] + 0] = R[D]
         D, A, B, I    ; NOTE: the first operand is the VALUE
         D, A, B, L    ; register, matching the DEST field and
                       ; the canonical disassembly "STORE R2, R1,
                       ; R0, 0" (instruction-set.spec section 7).
  ALLOC  D, A          ; DEST = new handle, size R[A] + 0
         D, A, I
         D, A, L
  FREE   A             ; invalidate handle R[A]
  JMP    A, I | I      ; IP = R[A] + sext(I); bare form uses R0
         A, L | L      ; IP = R[A] + label index; bare form R0
  BR     C, A, I | C, I
         C, A, L | C, L
  CALL   A, I | I
         A, L | L
  RET    (no operands)
  TRAP   I             ; trap code; labels are NOT accepted here,
                       ; because a trap code is not an address
  ADD    D, A, B
  SUB    D, A, B
  MUL    D, A, B
  DIV    D, A, B
  CMP    A, B          ; sets flags, writes no register; the
                       ; DEST field is emitted as 0
  AND    D, A, B
  OR     D, A, B
  XOR    D, A, B
  SHL    D, A, B
  SHR    D, A, B
  POPCNT D, A

Fields an opcode does not use are emitted as 0, except BR, whose
DEST field carries the condition code (instruction-set.spec
section 3). MOV with two operands emits IMM32 = 0. LOAD/STORE/
ALLOC with three/two operands emit IMM32 = 0.

No other opcode name is accepted. In particular, every
later-family mnemonic (Region, Relational, Retrieval, Agent,
Storage, Trust) is an "unknown opcode" error at assembly time,
even though the same byte values trap at run time when they
appear in a hand-built binary. Assembly time is stricter than
load time by design: the assembler only emits what Milestone 001
implements.

4. Labels
---------
A label is defined by "name:" at the start of a line (after
leading whitespace) or before an instruction on the same line
("loop: ADD R1, R1, R2"). Several labels may precede one
instruction ("a: b: RET"); all of them take the same value.
A label's value is the 0-based instruction index of the next
instruction emitted after the definition. A label defined after
the last instruction (a trailing label with no following
instruction) takes the value of one past the last instruction
index; jumping there halts (IP reaches exactly the end of code,
instruction-set.spec section 5), which is defined behavior, not
an assembler error.

References: a label may be referenced before its definition
(forward reference) or after it (backward reference). The
assembler is two-pass: pass 1 parses every line, collects all
label definitions, and records label references unresolved;
pass 2 resolves every reference to its instruction index and
emits the words. Errors:

- "duplicate label" when a name is defined twice, reported at
  the second definition's line.
- "undefined label" when a reference has no definition,
  reported at the referencing instruction's line.
- A label name that equals an opcode mnemonic (any case) or a
  register name is a "label uses a reserved name" error.
- A label name that equals a condition code is allowed; condition
  codes are only special in BR's first operand position.

A label used as an immediate (including in a jump operand) places
its instruction index in IMM32. Jump targets in the VM are
computed as R[SRC_A] + sext(IMM32) (instruction-set.spec
section 1), so a bare "JMP loop" (SRC_A = R0) lands on the label
only when R0 holds 0, which is its initial value. Programs that
clobber R0 must use an explicit zero-valued base register in
jump operands. The assembler performs no relocation and no
relative-offset computation: label values are absolute
instruction indices, exactly what the VM consumes.

5. Emission
-----------
The assembler emits one CODE section, exactly as
fixtures/gen.c builds a single-section image: 64-byte header,
one 32-byte section entry (type CODE, caps EXEC|READ, offset 96,
length = 8 * instruction count, alignment 8), entry-point offset
96 (instruction index 0), format version 1, ABI version 1,
target architecture 1, reserved field 0, and the SHA-256 digest
of all post-header bytes computed with machine/bsm_sha256.c.
Before writing the output file, the assembler validates its own
in-memory image with bsm_validate (machine/decoder/bsm_decoder.c)
and refuses to write anything if validation fails; that failure
is an assembler internal error (exit status 3), not a user
error, because a correct assembler cannot produce an invalid
image from a program it accepted.

6. Assembler errors and exit statuses
-------------------------------------
Errors are printed to stderr, one per line, in the form:

  <input-file>:<line>: error: <message>

The first error stops assembly; no output file is written (a
pre-existing output file is left untouched). Messages use the
exact phrases the verification harness asserts on:

  unknown opcode            bad register
  immediate out of range    malformed immediate
  undefined label           duplicate label
  wrong operand count       unknown condition code
  unknown directive         label uses a reserved name
  identifier too long       line too long
  no instructions           cannot read <file>
  cannot write <file>       internal validation failed

Exit statuses (documented here and in MILESTONE-002.md):

  0  Assembly succeeded; the output file was written and passed
     the assembler's own loader validation.
  1  Usage error (wrong number of command-line arguments).
  2  Assembly error (any message above except the last two).
  3  Host/internal error (cannot read input, cannot write output,
     out of memory, or the assembler's own output failed
     validation).

Usage: bsmasm <input.asm> <output.pqrb>

7. Examples
-----------
Loop summing 1..10 (fixtures/loop.asm):

  ; R1 = sum, R2 = counter, R3 = limit, R4 = step.
  MOV R1, R0, 0
  MOV R2, R0, 1
  MOV R3, R0, 11
  MOV R4, R0, 1
  loop:
  ADD R1, R1, R2
  ADD R2, R2, R4
  CMP R2, R3
  BR NE, loop
  RET

Subroutine call (fixtures/callret.asm):

  MOV R1, R0, 21
  CALL double
  MOV R3, R2
  RET
  double:
  ADD R2, R1, R1
  RET

8. Not in this milestone (documented, never silently accepted)
--------------------------------------------------------------
No directives, no macros, no conditional assembly, no data
sections or string literals, no multi-file assembly or linking,
no relative jumps, no language frontend, no new opcodes, no
floating-point, no guest input or syscalls. The canonical
disassembly in instruction-set.spec section 7 is valid assembler
input for every implemented opcode, except that disassembly of
an illegal opcode ("ILLEGAL 0x..") is not assemblable and is
correctly rejected as an unknown opcode.
