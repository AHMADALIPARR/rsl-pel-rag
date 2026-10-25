/* BSM-64 guest memory: typed handles, never host pointers.
 * ALLOC returns a small integer handle. LOAD/STORE address memory as
 * (handle, offset) and are bounds-checked against the allocation that
 * owns the handle. FREE invalidates the handle; the table keeps a
 * tombstone so later use reports use-after-free distinctly from use
 * of a handle that was never allocated, and a second FREE reports
 * double-free. Handle identifiers are never reused within one run. */
#ifndef BSM_MEMORY_H
#define BSM_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#define BSM_MAX_HANDLES 1024
#define BSM_MAX_ALLOC_BYTES (16u * 1024u * 1024u)

typedef enum {
    BSM_MEM_OK = 0,
    BSM_MEM_OOB,
    BSM_MEM_INVALID_HANDLE,
    BSM_MEM_USE_AFTER_FREE,
    BSM_MEM_DOUBLE_FREE,
    BSM_MEM_TOO_LARGE,
    BSM_MEM_TABLE_FULL,
    BSM_MEM_ALLOC_FAIL
} bsm_mem_err;

typedef struct {
    uint64_t handle;
    uint64_t size;
    uint8_t *data;
    int live; /* 1 = live, 0 = freed tombstone. */
} bsm_alloc_entry;

typedef struct {
    bsm_alloc_entry entries[BSM_MAX_HANDLES];
    uint64_t next_handle; /* Starts at 1; handle 0 is never valid. */
    size_t count;
} bsm_mem;

void bsm_mem_init(bsm_mem *m);
void bsm_mem_destroy(bsm_mem *m);
bsm_mem_err bsm_mem_alloc(bsm_mem *m, uint64_t size, uint64_t *handle_out);
bsm_mem_err bsm_mem_free(bsm_mem *m, uint64_t handle);
bsm_mem_err bsm_mem_load64(bsm_mem *m, uint64_t handle, uint64_t offset,
                           uint64_t *value_out);
bsm_mem_err bsm_mem_store64(bsm_mem *m, uint64_t handle, uint64_t offset,
                            uint64_t value);
const char *bsm_mem_err_name(bsm_mem_err e);

#endif
