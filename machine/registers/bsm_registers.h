/* BSM-64 register file: 32 x 64-bit general-purpose registers plus
 * the instruction pointer, the stack pointer, and explicit condition
 * flags Z, N, C, V. See constitution/instruction-set.spec for the
 * exact flag semantics of each opcode. */
#ifndef BSM_REGISTERS_H
#define BSM_REGISTERS_H

#include <stdint.h>

#define BSM_NREGS 32

typedef struct {
    uint64_t gpr[BSM_NREGS];
    uint64_t ip; /* Instruction index within the code section. */
    uint64_t sp; /* Depth of the VM return stack. */
    int z;       /* Zero: last flag-setting result was zero. */
    int n;       /* Negative: bit 63 of the last flag-setting result. */
    int c;       /* Carry / no-borrow, as defined per opcode. */
    int v;       /* Signed overflow of the last flag-setting result. */
} bsm_regs;

void bsm_regs_init(bsm_regs *r);
void bsm_regs_set_flags_add(bsm_regs *r, uint64_t a, uint64_t b,
                            uint64_t result);
void bsm_regs_set_flags_sub(bsm_regs *r, uint64_t a, uint64_t b,
                            uint64_t result);
void bsm_regs_set_flags_logic(bsm_regs *r, uint64_t result);

#endif
