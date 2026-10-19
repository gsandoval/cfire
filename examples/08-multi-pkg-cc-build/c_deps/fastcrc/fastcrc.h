#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t fastcrc_compute(const char* data, size_t len);

#ifdef __cplusplus
}
#endif
