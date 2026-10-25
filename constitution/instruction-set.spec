BSM-64 Instruction Set Specification (Milestone 001)
====================================================
Status: the subset below is implemented and verified by execution.
All other opcode families are reserved ranges only.

1. Instruction word
-------------------
Every instruction is one 64-bit little-endian word:

  Bits 63..56  OPCODE  (8 bits)
  Bits 55..48  DEST    (8 bits)
  Bits 47..40  SRC_A   (8 bits)
  Bits 39..32  SRC_B   (8 bits)
  Bits 31..0   IMM32   (32 bits)

IMM32 is interpreted as a signed 32-bit integer, sign-extended to
64 bits, wherever an opcode uses an immediate. Register fields
name registers R0..R31; a register field that an opcode uses must
be below 32, or the instruction traps with an invalid-register
trap. Fields an opcode does not use are ignored, even if nonzero,
except DEST for BR, which is the condition code (see below).

Registers start at zero. R0 is an ordinary register, not hardwired
to zero; programs that want a zero base register set one with MOV
or rely on the initial state. The instruction pointer (IP) counts
instructions from the start of the code section (instruction
index, not byte offset). Jump, branch, and call targets are
instruction indices, computed as R[SRC_A] + sign-extended IMM32.

2. Opcode map
-------------
Machine:
  0x01 MOV     0x02 LOAD    0x03 STORE   0x04 ALLOC   0x05 FREE
Control:
  0x10 JMP     0x11 BR      0x12 CALL    0x13 RET     0x14 TRAP
Arithmetic:
  0x20 ADD     0x21 SUB     0x22 MUL     0x23 DIV     0x24 CMP
Binary:
  0x30 AND     0x31 OR      0x32 XOR     0x33 SHL     0x34 SHR
  0x35 POPCNT

Reserved ranges (decoder traps with an illegal-opcode trap):
  0x40-0x4F Region      0x50-0x5F Relational
  0x60-0x6F Retrieval   0x70-0x7F Agent
  0x80-0x8F Storage     0x90-0x9F Trust
  0x00 and 0xA0-0xFF are also unassigned and trap identically.
No opcode outside the implemented list executes in this milestone.

3. Semantics per opcode
-----------------------
Machine:
  MOV   DEST = R[SRC_A] + sext(IMM32). Flags unchanged.
        (With SRC_A = a zero register this loads an immediate;
        with IMM32 = 0 it is a plain register copy.)
  LOAD  DEST = 64-bit little-endian value at
        (handle R[SRC_A], offset R[SRC_B] + sext(IMM32)).
        Bounds-checked; see memory-model.spec. Flags unchanged.
  STORE Memory at (handle R[SRC_A],
        offset R[SRC_B] + sext(IMM32)) = R[DEST].
        Bounds-checked. Flags unchanged.
  ALLOC DEST = new handle for a zero-filled allocation of
        R[SRC_A] + sext(IMM32) bytes. A negative size traps.
        Flags unchanged. See memory-model.spec for limits.
  FREE  Invalidates handle R[SRC_A]. Flags unchanged.

Control:
  JMP   IP = R[SRC_A] + sext(IMM32). Out-of-code targets trap.
  BR    DEST is the condition code, not a register:
          0 EQ (Z=1)   1 NE (Z=0)   2 MI (N=1)   3 PL (N=0)
          4 CS (C=1)   5 CC (C=0)   6 VS (V=1)   7 VC (V=0)
        If the condition holds, IP = R[SRC_A] + sext(IMM32);
        otherwise execution falls through. A condition code above
        7 traps. Out-of-code taken targets trap.
  CALL  Pushes the fall-through instruction index on the return
        stack, sets SP to the new depth, and jumps like JMP.
        Return-stack overflow (1024 deep) traps.
  RET   If the return stack is empty, the program halts normally
        (exit status 0). Otherwise pops the top return address
        into IP and sets SP to the new depth.
  TRAP  Traps unconditionally with trap code sext(IMM32).
        Exit status 3.

Arithmetic (all 64-bit, wraparound modulo 2^64):
  ADD   DEST = R[SRC_A] + R[SRC_B]. Sets Z, N, C, V.
  SUB   DEST = R[SRC_A] - R[SRC_B]. Sets Z, N, C, V.
  MUL   DEST = R[SRC_A] * R[SRC_B] (low 64 bits). Sets Z, N;
        clears C and V.
  DIV   Signed division: DEST = (int64)R[SRC_A] / (int64)R[SRC_B].
        Division by zero traps. INT64_MIN / -1 traps (division
        overflow); this is the specified choice for that case,
        not silent wraparound. Sets Z, N; clears C and V.
  CMP   Compares R[SRC_A] with R[SRC_B] exactly as SUB computes
        flags, but writes no register. Sets Z, N, C, V.

Binary:
  AND   DEST = R[SRC_A] & R[SRC_B]. Sets Z, N; clears C, V.
  OR    DEST = R[SRC_A] | R[SRC_B]. Sets Z, N; clears C, V.
  XOR   DEST = R[SRC_A] ^ R[SRC_B]. Sets Z, N; clears C, V.
  SHL   DEST = R[SRC_A] << (R[SRC_B] mod 64). Logical shift.
        Sets Z, N; clears C, V.
  SHR   DEST = R[SRC_A] >> (R[SRC_B] mod 64). Logical shift
        (zeros shift in; there is no arithmetic shift in M001).
        Sets Z, N; clears C, V.
  POPCNT DEST = number of set bits in R[SRC_A] (0..64).
        Sets Z, N; clears C, V.

4. Flags
--------
Z = 1 when the flag-setting result is zero, else 0.
N = bit 63 of the flag-setting result.
C = for ADD, unsigned carry out (1 when the sum wrapped);
    for SUB/CMP, no unsigned borrow (1 when R[SRC_A] >=
    R[SRC_B] as unsigned values); for all other flag-setting
    opcodes, 0.
V = for ADD, signed overflow (same-sign operands producing an
    opposite-sign result); for SUB/CMP, signed overflow
    (opposite-sign operands producing a result whose sign differs
    from R[SRC_A]); for all other flag-setting opcodes, 0.
MOV, LOAD, STORE, ALLOC, FREE, JMP, BR, CALL, RET, and TRAP never
change the flags. The initial flags are Z=0, N=0, C=0, V=0.

5. Traps and exit statuses
---------------------------
The VM exits with one of these statuses:
  0  Normal halt: RET on an empty return stack, or IP reaching
     exactly the end of the code section.
  1  Usage error (bad command-line arguments).
  2  Load / validation error (see binary-abi.spec). No guest
     instruction runs.
  3  Guest trap: illegal opcode, invalid register, invalid branch
     condition, division by zero, division overflow, out-of-code
     jump, return-stack overflow, guest TRAP, or any memory
     violation (see memory-model.spec). The trap name, the
     faulting instruction index, and the instruction count are
     printed to stderr.
  4  Instruction budget exceeded (see section 6).
  5  Host internal error (for example, host allocation failure).

A trap stops execution immediately; registers are not rolled back.

6. Execution budgets and determinism
------------------------------------
The default instruction budget is 1,000,000 executed instructions,
overridable with --max-instructions N. Reaching the budget is exit
status 4, not a halt. Execution is fully deterministic: the same
file, flags, and budget always produce the same register state,
the same halt/trap outcome, and byte-identical --trace output.
There is no input, no clock, no randomness, and no host state
visible to a guest program in this milestone.

7. Canonical disassembler
-------------------------
machine/decoder/bsm_decoder.c implements the canonical
disassembly used by --disasm and --trace. Format examples:

  MOV R1, R0, 42        ADD R3, R1, R2       CMP R1, R2
  LOAD R3, R1, R0, 0    STORE R2, R1, R0, 0  ALLOC R1, R0, 16
  FREE R1               JMP R0, 7           CALL R0, 4
  BR EQ, R0, 6          RET                  TRAP 7
  POPCNT R2, R1         SHL R3, R1, R2

Unknown opcodes disassemble as "ILLEGAL 0x.." so a trace never
conceals the instruction that trapped.

8. Not yet implemented (documented, never silently accepted)
------------------------------------------------------------
Language frontend, assembler, SQL, region algebra, relational and
retrieval opcodes, agent opcodes, storage opcodes, trust opcodes,
floating-point, host syscalls from guest code, and self-hosting.
Any opcode from a reserved range traps at run time.
