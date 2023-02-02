#pragma once

#include "pz_builtin.h"
#include <stdatomic.h>

#if defined(__x86_64__) || defined(__aarch64__)
    enum { PZ_ATOMIC_CACHE_LINE = 128 };
#elif defined(__arm__)
    enum { PZ_ATOMIC_CACHE_LINE = 32 };
#elif defined(__i386__)
    enum { PZ_ATOMIC_CACHE_LINE = 64 };
#else
    #error Architecture is not supported.
#endif

#if defined(__aarch64__) 
    #define pz_atomic_spin_loop_hint() __builtin_arm_isb(0xf) // SV
#elif defined(__arm__) && defined(__ARM_ARCH_7__)
    #define pz_atomic_spin_loop_hint() __builtin_arm_yield()
#elif defined(__i386__) || defined(__x86_64__)
    #define pz_atomic_spin_loop_hint() __builtin_ia32_pause()
#else
    #define pz_atomic_spin_loop_hint() __asm__ volatile ("" ::: "memory")
#endif
