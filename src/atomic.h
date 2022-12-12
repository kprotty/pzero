#ifndef _PZ_ATOMIC_H
#define _PZ_ATOMIC_H

#include "builtin.h"
#include <stdatomic.h>

typedef _Atomic(uintptr_t) atomic_uptr;

static FORCE_INLINE uintptr_t atomic_load_uptr(atomic_uptr* ptr) { return atomic_load_explicit(ptr, memory_order_relaxed); }
static FORCE_INLINE uintptr_t atomic_load_uptr_acquire(atomic_uptr* ptr) { return atomic_load_explicit(ptr, memory_order_acquire); }

static FORCE_INLINE void atomic_store_uptr(atomic_uptr* ptr, uintptr_t value) { atomic_store_explicit(ptr, value, memory_order_relaxed); }
static FORCE_INLINE void atomic_store_uptr_release(atomic_uptr* ptr, uintptr_t value) { atomic_store_explicit(ptr, value, memory_order_release); }

static FORCE_INLINE uintptr_t atomic_fetch_add_uptr_acquire(atomic_uptr* ptr, uintptr_t value) { return atomic_fetch_add_explicit(ptr, value, memory_order_acquire); }
static FORCE_INLINE uintptr_t atomic_fetch_add_uptr_release(atomic_uptr* ptr, uintptr_t value) { return atomic_fetch_add_explicit(ptr, value, memory_order_release); }
static FORCE_INLINE uintptr_t atomic_fetch_add_uptr_acq_rel(atomic_uptr* ptr, uintptr_t value) { return atomic_fetch_add_explicit(ptr, value, memory_order_acq_rel); }

static FORCE_INLINE uintptr_t atomic_swap_uptr_release(atomic_uptr* ptr, uintptr_t value) { return atomic_exchange_explicit(ptr, value, memory_order_release); }
static FORCE_INLINE uintptr_t atomic_swap_uptr_acq_rel(atomic_uptr* ptr, uintptr_t value) { return atomic_exchange_explicit(ptr, value, memory_order_acq_rel); }

static FORCE_INLINE uintptr_t atomic_cas_uptr_acquire(atomic_uptr* NOALIAS ptr, uintptr_t* NOALIAS cmp, uintptr_t xchg) { return atomic_compare_exchange_strong_explicit(ptr, cmp, xchg, memory_order_acquire, memory_order_relaxed); }
static FORCE_INLINE uintptr_t atomic_cas_uptr_acq_rel(atomic_uptr* NOALIAS ptr, uintptr_t* NOALIAS cmp, uintptr_t xchg) { return atomic_compare_exchange_strong_explicit(ptr, cmp, xchg, memory_order_acq_rel, memory_order_relaxed); }

#if defined(__x86_64) || defined(_M_X64)
    // Starting from Intel's Sandy Bridge, the spatial prefetcher pulls in pairs of 64-byte cache lines at a time.
    // - https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-optimization-manual.pdf
    // - https://github.com/facebook/folly/blob/1b5288e6eea6df074758f877c849b6e73bbb9fbb/folly/lang/Align.h#L107
    enum { ASSUMED_ATOMIC_CACHE_LINE = 128 };
#elif defined(__aarch64__) || defined(_M_ARM64)
    // Some big.LITTLE ARM archs have "big" cores with 128-byte cache lines:
    // - https://www.mono-project.com/news/2016/09/12/arm64-icache/
    // - https://cpufun.substack.com/p/more-m1-fun-hardware-information
    enum { ASSUMED_ATOMIC_CACHE_LINE = 128 };
#else
    enum { ASSUMED_ATOMIC_CACHE_LINE = 64 };
#endif

#endif // _PZ_ATOMIC_H
