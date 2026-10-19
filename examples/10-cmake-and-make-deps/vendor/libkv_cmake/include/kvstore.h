#ifndef CFIRE_KVSTORE_H
#define CFIRE_KVSTORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t kv_compute_slot(const char* key, uint32_t bucket_count);

#ifdef __cplusplus
}
#endif

#endif
