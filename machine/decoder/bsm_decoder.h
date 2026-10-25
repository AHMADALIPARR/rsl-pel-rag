/* BSM-64 binary format decoder, disassembler, and fixture validator.
 * This is the loader's validation logic, shared with the fixture
 * tool. Format details are specified in constitution/binary-abi.spec;
 * opcode semantics are specified in constitution/instruction-set.spec.
 * Nothing here executes guest code: validation either accepts the
 * whole file or rejects it before any guest instruction runs. */
#ifndef BSM_DECODER_H
#define BSM_DECODER_H

#include <stddef.h>
#include <stdint.h>

#define BSM_HEADER_SIZE 64
#define BSM_SECTION_ENTRY_SIZE 32
#define BSM_MAX_SECTIONS 16
#define BSM_FORMAT_VERSION 1
#define BSM_ABI_VERSION 1
#define BSM_TARGET_ARCH 1 /* BSM-64. */

#define BSM_SEC_NONE 0
#define BSM_SEC_CODE 1
#define BSM_SEC_DATA 2

#define BSM_CAP_EXEC 1u
#define BSM_CAP_READ 2u
#define BSM_CAP_WRITE 4u

/* Implemented opcode families (see instruction-set.spec). */
#define BSM_OP_MOV 0x01
#define BSM_OP_LOAD 0x02
#define BSM_OP_STORE 0x03
#define BSM_OP_ALLOC 0x04
#define BSM_OP_FREE 0x05
#define BSM_OP_JMP 0x10
#define BSM_OP_BR 0x11
#define BSM_OP_CALL 0x12
#define BSM_OP_RET 0x13
#define BSM_OP_TRAP 0x14
#define BSM_OP_ADD 0x20
#define BSM_OP_SUB 0x21
#define BSM_OP_MUL 0x22
#define BSM_OP_DIV 0x23
#define BSM_OP_CMP 0x24
#define BSM_OP_AND 0x30
#define BSM_OP_OR 0x31
#define BSM_OP_XOR 0x32
#define BSM_OP_SHL 0x33
#define BSM_OP_SHR 0x34
#define BSM_OP_POPCNT 0x35

/* Reserved ranges. The decoder traps on any of these at run time;
 * the format validator does not reject them, because a file that is
 * well-formed but contains a reserved opcode must fail as an
 * illegal-opcode trap, not as a load error. */
#define BSM_OP_RANGE_REGION_LO 0x40
#define BSM_OP_RANGE_REGION_HI 0x4f
#define BSM_OP_RANGE_RELATIONAL_LO 0x50
#define BSM_OP_RANGE_RELATIONAL_HI 0x5f
#define BSM_OP_RANGE_RETRIEVAL_LO 0x60
#define BSM_OP_RANGE_RETRIEVAL_HI 0x6f
#define BSM_OP_RANGE_AGENT_LO 0x70
#define BSM_OP_RANGE_AGENT_HI 0x7f
#define BSM_OP_RANGE_STORAGE_LO 0x80
#define BSM_OP_RANGE_STORAGE_HI 0x8f
#define BSM_OP_RANGE_TRUST_LO 0x90
#define BSM_OP_RANGE_TRUST_HI 0x9f

typedef struct {
    uint32_t type;
    uint32_t caps;
    uint64_t offset;
    uint64_t length;
    uint64_t align;
} bsm_section;

typedef struct {
    const uint8_t *data;
    size_t len;
    bsm_section sections[BSM_MAX_SECTIONS];
    uint32_t section_count;
    const uint8_t *code;
    size_t code_len;        /* Bytes; a multiple of 8. */
    uint64_t code_offset;   /* File offset of the code section. */
    uint64_t entry_index;   /* Instruction index of the entry point. */
} bsm_image;

/* Returns 0 on success, nonzero on rejection; err holds the reason. */
int bsm_validate(const uint8_t *data, size_t len, bsm_image *out,
                 char *err, size_t errcap);

uint64_t bsm_enc(uint8_t op, uint8_t dest, uint8_t src_a, uint8_t src_b,
                 int32_t imm);
uint8_t bsm_op(uint64_t word);
uint8_t bsm_dest(uint64_t word);
uint8_t bsm_src_a(uint64_t word);
uint8_t bsm_src_b(uint64_t word);
int32_t bsm_imm32(uint64_t word);
int64_t bsm_imm_sext(uint64_t word);

/* Canonical disassembly. Always succeeds; unknown opcodes disassemble
 * as ILLEGAL with their opcode byte, so a trace never hides a trap. */
void bsm_disasm(uint64_t word, char *buf, size_t cap);
const char *bsm_op_name(uint8_t op);
int bsm_op_implemented(uint8_t op);

uint16_t bsm_rd16le(const uint8_t *p);
uint32_t bsm_rd32le(const uint8_t *p);
uint64_t bsm_rd64le(const uint8_t *p);
void bsm_wr16le(uint8_t *p, uint16_t v);
void bsm_wr32le(uint8_t *p, uint32_t v);
void bsm_wr64le(uint8_t *p, uint64_t v);

#endif
