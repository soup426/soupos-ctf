#include "cpuid.h"

static void do_cpuid(uint32_t leaf, uint32_t subleaf,
                     uint32_t *eax, uint32_t *ebx,
                     uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile (
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(subleaf)
    );
}

/* Copy 4 bytes of a CPUID register into a char buffer */
static void reg2str(char *dst, uint32_t val) {
    dst[0] = (char)( val        & 0xFF);
    dst[1] = (char)((val >>  8) & 0xFF);
    dst[2] = (char)((val >> 16) & 0xFF);
    dst[3] = (char)((val >> 24) & 0xFF);
}

void cpuid_read(cpuid_info_t *info) {
    uint32_t eax, ebx, ecx, edx;

    /* Leaf 0: max standard leaf + vendor string (EBX:EDX:ECX) */
    do_cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    info->max_leaf = eax;
    reg2str(info->vendor + 0, ebx);
    reg2str(info->vendor + 4, edx);
    reg2str(info->vendor + 8, ecx);
    info->vendor[12] = '\0';

    /* Leaf 1: version info + feature flags */
    info->family = info->model = info->stepping = 0;
    info->logical_cores = 1;
    info->has_fpu = info->has_apic = info->has_sse  = info->has_sse2 = 0;
    info->has_sse3 = info->has_ssse3 = info->has_sse41 = info->has_sse42 = 0;
    info->has_aes  = info->has_avx  = 0;

    if (info->max_leaf >= 1) {
        do_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
        info->stepping      = eax & 0xF;
        info->model         = (eax >> 4) & 0xF;
        info->family        = (eax >> 8) & 0xF;
        /* Extended model/family for Intel/AMD */
        if (info->family == 6 || info->family == 15) {
            info->model  |= ((eax >> 12) & 0xF0);
            info->family += ((eax >> 20) & 0xFF);
        }
        info->logical_cores = (uint8_t)((ebx >> 16) & 0xFF);
        if (info->logical_cores == 0) info->logical_cores = 1;
        /* EDX feature flags */
        info->has_fpu   = (edx >>  0) & 1;
        info->has_apic  = (edx >>  9) & 1;
        info->has_sse   = (edx >> 25) & 1;
        info->has_sse2  = (edx >> 26) & 1;
        /* ECX feature flags */
        info->has_sse3  = (ecx >>  0) & 1;
        info->has_ssse3 = (ecx >>  9) & 1;
        info->has_sse41 = (ecx >> 19) & 1;
        info->has_sse42 = (ecx >> 20) & 1;
        info->has_aes   = (ecx >> 25) & 1;
        info->has_avx   = (ecx >> 28) & 1;
    }

    /* Extended leaves: processor brand string (3 × 16 chars) */
    do_cpuid(0x80000000u, 0, &eax, &ebx, &ecx, &edx);
    if (eax >= 0x80000004u) {
        for (int i = 0; i < 3; i++) {
            do_cpuid((uint32_t)(0x80000002u + (uint32_t)i), 0,
                     &eax, &ebx, &ecx, &edx);
            reg2str(info->brand + i * 16 +  0, eax);
            reg2str(info->brand + i * 16 +  4, ebx);
            reg2str(info->brand + i * 16 +  8, ecx);
            reg2str(info->brand + i * 16 + 12, edx);
        }
        info->brand[48] = '\0';
        /* Trim leading spaces that some CPUs emit */
        char *b = info->brand;
        while (*b == ' ') b++;
        if (b != info->brand) {
            /* Shift string left */
            char *d = info->brand;
            while (*b) *d++ = *b++;
            *d = '\0';
        }
    } else {
        info->brand[0] = '\0';
    }
}
