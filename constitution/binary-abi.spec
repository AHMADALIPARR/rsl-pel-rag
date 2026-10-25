BSM-64 Binary ABI Specification (Milestone 001)
===============================================
Status: implemented and verified by execution in this milestone.
Version: format version 1, ABI version 1.

1. File layout
---------------
A BSM-64 executable ("PQRB file") is a byte stream:

  [ 64-byte initial header ][ section table ][ section bodies ]

All multi-byte integers are little-endian. There is no alignment
padding between the header, the table, and the bodies other than
what the section offsets themselves require.

2. The 64-byte initial header
------------------------------
Offset  Size  Field
0       4     Magic: ASCII "PQRB" (0x50 0x51 0x52 0x42).
4       2     Format version, must be 1.
6       2     ABI version, must be 1.
8       4     Target architecture, must be 1 (BSM-64).
12      4     Header size, must be 64.
16      8     Entry-point offset: byte offset in the file of the
              first instruction to execute.
24      4     Section count, 1..16.
28      4     Reserved, must be 0.
32      32    Content digest (see section 5).

3. Section table
----------------
Immediately after the header, one 32-byte entry per section:

Offset  Size  Field
0       4     Section type: 1 = CODE, 2 = DATA. No other value
              is accepted.
4       4     Capability mask (see capability-model.spec):
              bit 0 (1) = EXEC, bit 1 (2) = READ, bit 2 (4) = WRITE.
              No other bits may be set.
8       8     Offset of the section body in the file.
16      8     Length of the section body in bytes.
24      8     Alignment: a power of two, 1..4096. The section
              offset must be a multiple of the alignment.

Every section specifies type, offset, length, alignment, and
capability mask. Validation requirements for every section:

  - The body must lie entirely within the file.
  - The body must not overlap the header or the section table.
  - No two section bodies may overlap each other.
  - A CODE section's length must be nonzero and a multiple of 8
    (one instruction word is 8 bytes).
  - A CODE section must carry the EXEC capability.
  - At least one CODE section must exist.

4. Entry point
--------------
The entry-point offset must lie inside a CODE section and must be
aligned to 8 bytes relative to that section's start. The loader
converts it to an instruction index (offset minus section start,
divided by 8) before execution begins. An entry point anywhere
else is a load error, not a run-time trap.

5. Content digest
-----------------
The digest field (header bytes 32..63) holds SHA-256 of every byte
of the file AFTER the 64-byte header: that is, of the section
table concatenated with the section bodies, in file order. The
digest is computed with the hand-written SHA-256 implementation in
machine/bsm_sha256.c (FIPS 180-4), shared by the loader and by the
fixture tool, and (since Milestone 002) by the assembler, so any
tool that writes a file and the loader that checks it cannot drift
apart.

The digest binds the section table and all section bodies. Header
fields other than the digest (magic, versions, entry offset,
section count) are validated directly, field by field, and are
therefore covered by validation even though they are outside the
hashed region. No cryptographic property beyond "this is the
SHA-256 digest of the post-header bytes" is claimed: the digest
detects accidental corruption and tampering with section contents
by anyone who does not recompute it, and nothing more.

6. Loader rejection (before any guest instruction runs)
-------------------------------------------------------
The loader (machine/decoder/bsm_decoder.c, function bsm_validate)
rejects a file, with exit status 2, if any of the following holds:

  - File shorter than 64 bytes (truncated header).
  - Bad magic, bad format version, bad ABI version, bad target
    architecture, header size other than 64, or nonzero reserved
    field.
  - Section count 0 or above 16, or a section table truncated by
    end of file.
  - Digest mismatch.
  - Unknown section type, unknown capability bit, bad alignment
    (zero, above 4096, or not a power of two), or a misaligned
    section offset.
  - A section body out of file bounds, overlapping the header or
    table, or overlapping another section.
  - No CODE section, a CODE section without EXEC, or a CODE
    section length that is not a nonzero multiple of 8.
  - Entry point outside every CODE section, or misaligned within
    its section.

Rejection happens entirely before execution: no guest instruction
from a rejected file is ever fetched. A well-formed file whose
code contains a reserved or unknown opcode is NOT rejected here;
that failure is an illegal-opcode trap at run time (exit status 3),
so the two failure classes stay distinguishable.

7. Reserved extension fields
----------------------------
Header bytes 28..31 are reserved and must be zero; they are the
extension point for future header fields. A future format version
may define them; format version 1 files with nonzero reserved
bytes are rejected rather than silently reinterpreted. Section
types and capability bits not defined above are likewise rejected
at load time, not ignored.

8. What is NOT in this milestone
--------------------------------
No symbol table, no relocations, no dynamic linking, no debug
sections, no compression. A later milestone may add section types
and header fields under a new format version; format version 1
files must keep validating exactly as specified here.
