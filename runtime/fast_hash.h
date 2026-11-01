#ifndef CFIRE_VENDOR_FAST_HASH_H
#define CFIRE_VENDOR_FAST_HASH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t fh_fnv1a(const char* data, size_t len) {
    uint32_t h = 0x811c9dc5u;
    for (size_t i = 0; i < len; ++i) {
        h ^= (uint8_t)data[i];
        h *= 0x01000193u;
    }
    return h;
}

#ifdef __cplusplus
}
#endif

#endif // CFIRE_VENDOR_FAST_HASH_H
