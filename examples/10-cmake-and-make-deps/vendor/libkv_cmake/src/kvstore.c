#include "kvstore.h"

#ifndef KV_SALT
#define KV_SALT 17
#endif

uint32_t kv_compute_slot(const char* key, uint32_t bucket_count) {
    if (!key || bucket_count == 0) return 0;
    uint32_t h = (uint32_t)KV_SALT;
    while (*key) {
        h = (h * 33u) ^ (uint8_t)(*key++);
    }
    return h % bucket_count;
}
