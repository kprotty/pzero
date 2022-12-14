#ifndef _PZ_ATOMIC_H
#define _PZ_ATOMIC_H

#include "builtin.h"
#include "random.h"
#include <stdatomic.h>

typedef _Atomic(uintptr_t) atomic_uptr;

static FORCE_INLINE uintptr_t atomic_load_uptr(atomic_uptr* ptr) { return atomic_load_explicit(ptr, memory_order_relaxed); }
static FORCE_INLINE uintptr_t atomic_load_uptr_acquire(atomic_uptr* ptr) { return atomic_load_explicit(ptr, memory_order_acquire); }

static FORCE_INLINE void atomic_store_uptr(atomic_uptr* ptr, uintptr_t value) { atomic_store_explicit(ptr, value, memory_order_relaxed); }
static FORCE_INLINE void atomic_store_uptr_release(atomic_uptr* ptr, uintptr_t value) { atomic_store_explicit(ptr, value, memory_order_release); }

static FORCE_INLINE uintptr_t atomic_fetch_add_uptr_acquire(atomic_uptr* ptr, uintptr_t value) { return atomic_fetch_add_explicit(ptr, value, memory_order_acquire); }
static FORCE_INLINE uintptr_t atomic_fetch_add_uptr_release(atomic_uptr* ptr, uintptr_t value) { return atomic_fetch_add_explicit(ptr, value, memory_order_release); }
static FORCE_INLINE uintptr_t atomic_fetch_sub_uptr_release(atomic_uptr* ptr, uintptr_t value) { return atomic_fetch_sub_explicit(ptr, value, memory_order_release); }
static FORCE_INLINE uintptr_t atomic_fetch_add_uptr_acq_rel(atomic_uptr* ptr, uintptr_t value) { return atomic_fetch_add_explicit(ptr, value, memory_order_acq_rel); }

static FORCE_INLINE uintptr_t atomic_swap_uptr_acquire(atomic_uptr* ptr, uintptr_t value) { return atomic_exchange_explicit(ptr, value, memory_order_acquire); }
static FORCE_INLINE uintptr_t atomic_swap_uptr_release(atomic_uptr* ptr, uintptr_t value) { return atomic_exchange_explicit(ptr, value, memory_order_release); }
static FORCE_INLINE uintptr_t atomic_swap_uptr_acq_rel(atomic_uptr* ptr, uintptr_t value) { return atomic_exchange_explicit(ptr, value, memory_order_acq_rel); }

static FORCE_INLINE uintptr_t atomic_cas_uptr_acquire(atomic_uptr* NOALIAS ptr, uintptr_t* NOALIAS cmp, uintptr_t xchg) { return atomic_compare_exchange_weak_explicit(ptr, cmp, xchg, memory_order_acquire, memory_order_acquire); }
static FORCE_INLINE uintptr_t atomic_cas_uptr_acq_rel(atomic_uptr* NOALIAS ptr, uintptr_t* NOALIAS cmp, uintptr_t xchg) { return atomic_compare_exchange_weak_explicit(ptr, cmp, xchg, memory_order_acq_rel, memory_order_acquire); }

#if defined(ARCH_X64)
    // Starting from Intel's Sandy Bridge, the spatial prefetcher pulls in pairs of 64-byte cache lines at a time.
    // - https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-optimization-manual.pdf
    // - https://github.com/facebook/folly/blob/1b5288e6eea6df074758f877c849b6e73bbb9fbb/folly/lang/Align.h#L107
    enum { ASSUMED_ATOMIC_CACHE_LINE = 128 };
#elif defined(ARCH_ARM64)
    // Some big.LITTLE ARM archs have "big" cores with 128-byte cache lines:
    // - https://www.mono-project.com/news/2016/09/12/arm64-icache/
    // - https://cpufun.substack.com/p/more-m1-fun-hardware-information
    enum { ASSUMED_ATOMIC_CACHE_LINE = 128 };
#elif defined(ARCH_ARM)
    // This platforms reportedly have 32-byte cache lines
    // - https://github.com/golang/go/blob/3dd58676054223962cd915bb0934d1f9f489d4d2/src/internal/cpu/cpu_arm.go#L7
    enum { ASSUMED_ATOMIC_CACHE_LINE = 32 };
#else
    // Other x86 and WASM platforms have 64-byte cache lines.
    // The rest of the architectures are assumed to be similar.
    // - https://github.com/golang/go/blob/dda2991c2ea0c5914714469c4defc2562a907230/src/internal/cpu/cpu_x86.go#L9
    // - https://github.com/golang/go/blob/3dd58676054223962cd915bb0934d1f9f489d4d2/src/internal/cpu/cpu_wasm.go#L7
    enum { ASSUMED_ATOMIC_CACHE_LINE = 64 };
#endif

#if defined(OS_WINDOWS)
    #define atomic_hint_yield YieldProcessor
#elif defined(ARCH_X64) || defined(ARCH_X86)
    #define atomic_hint_yield __builtin_ia32_pause
#elif defined(OS_DARWIN) && defined(ARCH_ARM64)
    #define atomic_hint_yield __builtin_arm_wfe
#elif defined(ARCH_ARM) || defined(ARCH_ARM64)
    #define atomic_hint_yield __builtin_arm_yield()
#else
    #error "architecture not supported for emitting CPU yield hint"
#endif

static void atomic_hint_backoff(struct pz_random* rng) {
    uint32_t num_spins;

    #if defined(OS_DARWIN) && defined(ARCH_ARM64)
        // On iOS and M1/M2, a single WFE instruction provides enough backoff delay
        // while also sleeping efficiently unlike with the generic YIELD instruction.
        num_spins = 1;
    #else
        // Decide a random amount of times to spin between 32 and 128.
        // Unlike exponential backoff, this avoids the threads constantly contending at the same rate.
        // Uses the top bits of the random value, assuming they have the most entropy.
        // https://github.com/apple/swift-corelibs-libdispatch/blob/main/src/shims/yield.h#L102-L125
        num_spins = ((pz_random_next(rng) >> 24) & (128 - 1)) | (32 - 1);
    #endif

    while (CHECKED_SUB(num_spins, 1, &num_spins)) {
        atomic_hint_yield();
    }
}



#endif // _PZ_ATOMIC_H
