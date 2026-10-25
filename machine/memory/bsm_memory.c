#include "bsm_memory.h"

#include <stdlib.h>
#include <string.h>

void bsm_mem_init(bsm_mem *m) {
    memset(m, 0, sizeof(*m));
    m->next_handle = 1;
}

void bsm_mem_destroy(bsm_mem *m) {
    size_t i;
    for (i = 0; i < m->count; i++) {
        free(m->entries[i].data);
        m->entries[i].data = NULL;
    }
    m->count = 0;
}

static bsm_alloc_entry *find_entry(bsm_mem *m, uint64_t handle) {
    size_t i;
    for (i = 0; i < m->count; i++) {
        if (m->entries[i].handle == handle) {
            return &m->entries[i];
        }
    }
    return NULL;
}

bsm_mem_err bsm_mem_alloc(bsm_mem *m, uint64_t size, uint64_t *handle_out) {
    bsm_alloc_entry *e;
    if (size > BSM_MAX_ALLOC_BYTES) {
        return BSM_MEM_TOO_LARGE;
    }
    if (m->count >= BSM_MAX_HANDLES) {
        return BSM_MEM_TABLE_FULL;
    }
    e = &m->entries[m->count];
    e->data = (uint8_t *)calloc(1, size == 0 ? 1 : (size_t)size);
    if (e->data == NULL) {
        return BSM_MEM_ALLOC_FAIL;
    }
    e->handle = m->next_handle++;
    e->size = size;
    e->live = 1;
    *handle_out = e->handle;
    m->count++;
    return BSM_MEM_OK;
}

bsm_mem_err bsm_mem_free(bsm_mem *m, uint64_t handle) {
    bsm_alloc_entry *e = find_entry(m, handle);
    if (e == NULL) {
        return BSM_MEM_INVALID_HANDLE;
    }
    if (!e->live) {
        return BSM_MEM_DOUBLE_FREE;
    }
    free(e->data);
    e->data = NULL;
    e->live = 0;
    return BSM_MEM_OK;
}

static bsm_mem_err check_access(bsm_mem *m, uint64_t handle, uint64_t offset,
                                bsm_alloc_entry **entry_out) {
    bsm_alloc_entry *e = find_entry(m, handle);
    if (e == NULL) {
        return BSM_MEM_INVALID_HANDLE;
    }
    if (!e->live) {
        return BSM_MEM_USE_AFTER_FREE;
    }
    if (offset > e->size || e->size - offset < 8) {
        return BSM_MEM_OOB;
    }
    *entry_out = e;
    return BSM_MEM_OK;
}

bsm_mem_err bsm_mem_load64(bsm_mem *m, uint64_t handle, uint64_t offset,
                           uint64_t *value_out) {
    bsm_alloc_entry *e = NULL;
    bsm_mem_err err = check_access(m, handle, offset, &e);
    uint64_t value = 0;
    int i;
    if (err != BSM_MEM_OK) {
        return err;
    }
    for (i = 0; i < 8; i++) {
        value |= ((uint64_t)e->data[offset + (uint64_t)i]) << (8 * i);
    }
    *value_out = value;
    return BSM_MEM_OK;
}

bsm_mem_err bsm_mem_store64(bsm_mem *m, uint64_t handle, uint64_t offset,
                            uint64_t value) {
    bsm_alloc_entry *e = NULL;
    bsm_mem_err err = check_access(m, handle, offset, &e);
    int i;
    if (err != BSM_MEM_OK) {
        return err;
    }
    for (i = 0; i < 8; i++) {
        e->data[offset + (uint64_t)i] = (uint8_t)(value >> (8 * i));
    }
    return BSM_MEM_OK;
}

const char *bsm_mem_err_name(bsm_mem_err e) {
    switch (e) {
    case BSM_MEM_OK:
        return "ok";
    case BSM_MEM_OOB:
        return "out-of-bounds access";
    case BSM_MEM_INVALID_HANDLE:
        return "invalid handle";
    case BSM_MEM_USE_AFTER_FREE:
        return "use-after-free";
    case BSM_MEM_DOUBLE_FREE:
        return "double-free";
    case BSM_MEM_TOO_LARGE:
        return "allocation too large";
    case BSM_MEM_TABLE_FULL:
        return "handle table full";
    case BSM_MEM_ALLOC_FAIL:
        return "allocation failed";
    default:
        return "unknown memory error";
    }
}
