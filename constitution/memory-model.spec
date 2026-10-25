BSM-64 Memory Model Specification (Milestone 001)
================================================
Status: implemented and verified by execution in this milestone.

1. Principle
------------
A guest program never sees a host pointer. The only memory it can
name is an allocation it created with ALLOC, addressed through a
small integer handle plus a byte offset. There is no guest-visible
address space, no pointer arithmetic on host addresses, and no way
to name memory that does not belong to a live handle.

2. Handles
----------
ALLOC returns a handle: an integer starting at 1 and increasing
by one per allocation within a run. Handle 0 is never valid. A
handle identifies exactly one allocation for the whole run;
handles are never reused, even after FREE, so a stale handle
cannot accidentally name a later allocation.

Limits in this milestone: at most 1024 handles per run, and at
most 16 MiB per allocation. Exceeding either traps (handle table
full / allocation too large). Allocations are zero-filled.
There is no guest-visible heap limit beyond these two caps, and
no garbage collector: reclamation is explicit FREE only.

3. LOAD and STORE
-----------------
LOAD and STORE move exactly 8 bytes, little-endian, at
(handle, offset), where offset = R[SRC_B] + sign-extended IMM32.
The access is valid only when all of the following hold:

  - The handle was returned by ALLOC in this run.
  - The handle has not been freed.
  - offset >= 0 and offset + 8 <= allocation size.

A negative computed offset traps as out-of-bounds. Partial
overlaps are not permitted: an 8-byte access must fit entirely
inside the allocation. There are no unaligned-access traps in
this milestone because every access is 8 bytes at an arbitrary
byte offset within an allocation and is assembled byte by byte;
alignment of the offset is not required.

4. Ownership, FREE, and violations
----------------------------------
ALLOC creates ownership of the new handle by the guest program.
FREE relinquishes it. The handle table keeps a tombstone for a
freed handle, so violations are reported distinctly:

  - LOAD/STORE through a handle that was never allocated
    (including handle 0): invalid-handle trap.
  - LOAD/STORE through a freed handle: use-after-free trap.
  - FREE of a handle that was never allocated: invalid-handle
    trap.
  - FREE of an already-freed handle: double-free trap.
  - LOAD/STORE with offset + 8 beyond the allocation, or with a
    negative offset: out-of-bounds trap.

Every violation is a guest trap (exit status 3) that stops
execution immediately. No violation is recoverable inside the
guest in this milestone, and no violation corrupts another
allocation: the bounds check happens before any byte is read
or written.

5. Isolation
------------
Guest memory is host memory allocated by the VM and indexed only
through the handle table. The VM's own structures (register file,
return stack, code image) are not addressable through any handle,
and guest code has no instruction that yields a host address.
Two allocations are disjoint by construction: each handle owns
its own byte array, and an access through one handle can never
reach another handle's bytes, because the bounds check is
against the accessed handle's own size.

6. What is NOT in this milestone
--------------------------------
No shared memory, no memory-mapped files, no guest-visible stack
in memory (the return stack is VM-internal and addressed only by
CALL/RET), no protection rings beyond the handle checks above,
no capability transfer between handles, and no persistence of
allocations across runs. Region algebra and the storage family
are later-milestone work and are not present here.
