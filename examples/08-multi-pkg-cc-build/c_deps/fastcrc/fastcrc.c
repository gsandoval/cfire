#include "fastcrc.h"

#ifndef FASTCRC_SEED
#define FASTCRC_SEED 1
#endif

uint32_t fastcrc_compute(const char* data, size_t len) {
    uint32_t acc = (uint32_t)FASTCRC_SEED;
    for (size_t i = 0; i < len; ++i) {
        acc = (acc * 33u) + (uint8_t)data[i];
    }
    return acc;
}
