BSM-64 Capability Model Specification (Milestone 001)
=====================================================
Status: the minimal model below is implemented and verified by
execution. Richer capability transitions are later-milestone work.

1. Principle
------------
In this milestone a capability is a property declared on a
section and checked by the loader and by the VM. Nothing in a
guest program grants itself a capability: capabilities come only
from the section table of the file being loaded, and the VM
enforces them. A program that needs a capability it was not
given fails at load time or traps at run time; it never proceeds
without the capability.

2. The section capability mask
-------------------------------
Each section declares a mask in its table entry (binary-abi.spec
section 3):

  bit 0 (1) = EXEC   The section may be fetched as code.
  bit 1 (2) = READ   The section may be read.
  bit 2 (4) = WRITE  The section may be written.

No other bits may be set; the loader rejects unknown bits. In
this milestone the mask is descriptive for DATA sections and
enforced for CODE sections, as follows.

3. What the loader validates
-----------------------------
Before any guest instruction runs, the loader checks:

  - Every CODE section carries EXEC. A CODE section without EXEC
    is a load error (exit status 2).
  - The entry point lies inside a CODE section (which therefore
    carries EXEC) and is 8-byte aligned within it.
  - The digest covers the section table, so the capability masks
    cannot be altered without invalidating the digest.
  - Section bodies do not overlap, so no byte of the file can be
    both code under one section and data under another in a way
    that would let one capability mask launder another.

4. What the VM enforces at run time
------------------------------------
  - Fetch capability: every fetched instruction comes from the
    validated EXEC code section. The instruction pointer is an
    index into that section only; a jump, branch, or call target
    outside it traps as an out-of-code jump (exit status 3).
    There is no fetch from a DATA section and no fetch from
    outside the file, because the only fetch path indexes the
    code section.
  - Memory capability: LOAD/STORE do not operate on file
    sections at all in this milestone; they operate on handles
    (see memory-model.spec). A handle is itself the capability
    for its allocation: possession of a live handle is what
    authorises access, and FREE revokes it. Access without a
    live handle traps.
  - Execution budget: the instruction cap (default 1,000,000,
    overridable by flag) bounds every run; exceeding it exits
    with status 4.

5. Tests proving enforcement
----------------------------
The verification harness proves each enforced check with a
failing program, not with a description:

  - A file whose entry point lies outside its code section is
    rejected at load time (entryoutside fixture, exit 2).
  - A program that jumps outside the code section traps at run
    time (jumpoutside fixture, exit 3): this is the runtime
    fetch-capability check.
  - A program whose code bytes contain a reserved opcode traps
    at run time (illegalopcode fixture, exit 3).
  - Memory capability violations trap: out-of-bounds load and
    store, unallocated-handle load, double-free, and
    use-after-free fixtures, each exit 3.

6. What is NOT in this milestone (documented, not implied)
----------------------------------------------------------
No per-handle read/write capability bits (a live handle permits
both LOAD and STORE in M001); no capability creation, delegation,
or revocation instructions; no capability transitions on CALL/RET
beyond the return stack itself; no section-granular enforcement
for DATA section READ/WRITE at run time, because guest LOAD/STORE
address handles, not file sections, and DATA sections are not
yet guest-addressable. Any of these, if assumed by a reader, is a
misreading of this milestone: the honest statement is that M001
capabilities are the section EXEC mask plus handle possession,
enforced as described above, and the rest is future work.
