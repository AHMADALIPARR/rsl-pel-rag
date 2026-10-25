/* BSM-64 SHA-256 (FIPS 180-4), hand-written, no external libraries.
 * Shared by the loader (digest verification) and the fixture tool
 * (digest computation). This implementation provides the SHA-256
 * compression function and padding exactly as specified by the
 * standard; it makes no claim beyond computing SHA-256 digests. */
#ifndef BSM_SHA256_H
#define BSM_SHA256_H

#include <stddef.h>
#include <stdint.h>

void bsm_sha256(const uint8_t *data, size_t len, uint8_t out[32]);

#endif
