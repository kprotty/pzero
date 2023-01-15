#ifndef _PZ_ATOMIC_H
#define _PZ_ATOMIC_H

#include "builtin.h"
#include <stdatomic.h>

enum {
    #if defined(__x86_64__) || defined(__aarch64__)
        PZ_ATOMIC_CACHE_LINE = 128
    #elif defined(__arm__)
        PZ_ATOMIC_CACHE_LINE = 32
    #else
        PZ_ATOMIC_CACHE_LINE = 64
    #endif  
};

static inline void pz_atomic_yield(void) {
    #if defined(__aarch64__)
        __builtin_arm_isb(0xf); // Use ISB(SY) over yield as it pauses most efficiently.
    #elif defined(__arm__) || defined(__arm64__) || defined(__thumb__)
        __builtin_arm_yield();
    #elif defined(__i386__) || defined(__x86_64__)
        __builtin_ia32_pause();
    #else
        #error "cpu architecture not supported"
    #endif
}

static inline void pz_atomic_backoff(void) {
    // A simple LCG pseudo-random number generator (based on old libc's random).
    // It's ok that loads/stores are used as it may only end up replaying random values.
    // This function is called in response to contention, so introducing more RMWs wouldn't help.
    static _Atomic(uint32_t) seed = 1;
    uint32_t rng = atomic_load_explicit(&seed, memory_order_relaxed);
    uint32_t next = PZ_WRAPPING_ADD(PZ_WRAPPING_MUL(rng, 1103515245), 12345);
    atomic_store_explicit(&seed, next, memory_order_relaxed);

    // Spin for MIN(128, MAX(32, R)) where R = highest bits of entropy from RNG.
    uint32_t spin_min = 32 - 1, spin_max = 128 - 1;
    uint32_t spin = ((rng >> 24) & spin_min) | spin_max;
    while (PZ_OVERFLOW_SUB(spin, 1, &spin)) {
        pz_atomic_yield();
    }
}

static void* pz_atomic_mwait(_Atomic(void*)* ptr) {
    // Initial load to see if there's a value.
    void* value;
    if (PZ_LIKELY(value = atomic_load_explicit(ptr, memory_order_acquire))) {
        return value;
    }

    // Spin for a bit using WFE on Apple's ARM chips as it's the most efficient way to sleep.
    // https://github.com/apple/swift-corelibs-libdispatch/blob/469c8ecfa0011f5da23acacf8658b6a60a899a78/src/shims/yield.c#L44-L54
    #if (defined(__arm__) && defined(__APPLE__)) || defined(__arm64__)
        uint32_t spin = 10;
        while (PZ_LIKELY(PZ_OVERFLOW_SUB(spin, 1, &spin))) {
            if (PZ_LIKELY(value = __builtin_arm_ldaex(ptr))) {
                __builtin_arm_clrex();
                return value;
            }
            __builtin_arm_wfe();
        }

    // Emulate NTDLL by spinning using CyclesPerYield or MWAITX and only when other threads are active.
    #elif defined(_WIN32)
        uint16_t UnparkedProcessorCount = *((volatile uint16_t*)(0x7ffe0000 + 0x036a));
        uint16_t CyclesPerYield = *((volatile uint16_t*)(0x7ffe0000 + 0x02d6));
        uint16_t MaxSpinCycles = 0x2800;

        if (PZ_LIKELY(UnparkedProcessorCount > 1)) {
            uint32_t spin = MaxSpinCycles / CyclesPerYield;
            while (PZ_LIKELY(PZ_OVERFLOW_SUB(spin, 1, &spin))) {
                pz_atomic_yield();
                if (PZ_LIKELY(value = atomic_load_explicit(ptr, memory_order_acquire))) {
                    return value;
                }
            }
        }

    // Spin for a bit trying to load the value.
    // https://github.com/apple/swift-corelibs-libdispatch/blob/469c8ecfa0011f5da23acacf8658b6a60a899a78/src/shims/yield.c#L54-L63
    #else
        uint32_t spins = 128;
        while (PZ_LIKELY(PZ_OVERFLOW_SUB(spin, 1, &spin))) {
            pz_atomic_yield();
            if (PZ_LIKELY(value = atomic_load_explicit(ptr, memory_order_acquire))) {
                return value;
            }
        }
    #endif

    return NULL;
}

#endif // _PZ_ATOMIC_H
