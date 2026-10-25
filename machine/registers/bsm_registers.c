#include "bsm_registers.h"

#include <string.h>

void bsm_regs_init(bsm_regs *r) {
    memset(r, 0, sizeof(*r));
}

void bsm_regs_set_flags_add(bsm_regs *r, uint64_t a, uint64_t b,
                            uint64_t result) {
    r->z = (result == 0);
    r->n = (int)((result >> 63) & 1u);
    r->c = (result < a); /* Unsigned carry out: result wrapped below a. */
    /* Signed overflow: same-sign operands, result of opposite sign. */
    r->v = ((((a ^ result) & (b ^ result)) >> 63) & 1u) != 0;
}

void bsm_regs_set_flags_sub(bsm_regs *r, uint64_t a, uint64_t b,
                            uint64_t result) {
    r->z = (result == 0);
    r->n = (int)((result >> 63) & 1u);
    r->c = (a >= b); /* No unsigned borrow. */
    /* Signed overflow: operands of opposite sign, result sign != a. */
    r->v = ((((a ^ b) & (a ^ result)) >> 63) & 1u) != 0;
}

void bsm_regs_set_flags_logic(bsm_regs *r, uint64_t result) {
    r->z = (result == 0);
    r->n = (int)((result >> 63) & 1u);
    r->c = 0;
    r->v = 0;
}
