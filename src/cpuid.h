#pragma once
#include <stdint.h>

typedef struct {
    char     vendor[13];     /* e.g. "GenuineIntel\0" */
    char     brand[49];      /* e.g. "Intel(R) Core(TM) i7...\0" */
    uint32_t max_leaf;
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    uint8_t  logical_cores;
    int      has_fpu;
    int      has_apic;
    int      has_sse;
    int      has_sse2;
    int      has_sse3;
    int      has_ssse3;
    int      has_sse41;
    int      has_sse42;
    int      has_aes;
    int      has_avx;
} cpuid_info_t;

void cpuid_read(cpuid_info_t *info);
