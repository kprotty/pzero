/* Copyright (c) 2020 kprotty
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef PZ_ATOMIC_H
#define PZ_ATOMIC_H
    #include "pz.h"

    #if defined(_MSC_VER) || defined(__cplusplus)
        #include <atomic>

        #define PzAtomic(type) std::atomic<type>

        #define PZ_ATOMIC_RELAXED std::memory_order_relaxed
        #define PZ_ATOMIC_ACQUIRE std::memory_order_acquire
        #define PZ_ATOMIC_RELEASE std::memory_order_release
        #define PZ_ATOMIC_ACQ_REL std::memory_order_acq_rel
        #define PZ_ATOMIC_SEQ_CST std::memory_order_seq_cst

        #define PzAtomicLoad(type, ptr, ordering) \
            (((PzAtomic(type)*)(ptr)).load(value, ordering))

        #define PzAtomicStore(type, ptr, value, ordering) \
            (((PzAtomic(type)*)(ptr)).store(value, ordering))

        #define PzAtomicSwap(type, ptr, value, ordering) \
            (((PzAtomic(type)*)(ptr)).exchange(value, ordering))

        #define PzAtomicFetchAdd(type, ptr, value, ordering) \
            (((PzAtomic(type)*)(ptr)).fetch_add(value, ordering))

        #define PzAtomicFetchSub(type, ptr, value, ordering) \
            (((PzAtomic(type)*)(ptr)).fetch_sub(value, ordering))

        #define PzAtomicCompareExchangeWeak(type, ptr, cmp_ptr, xchg, success, failure) \
            (((PzAtomic(type)*)(ptr)).compare_exchange_weak(cmp_ptr, xchg, success, failure))

        #define PzAtomicCompareExchangeStrong(type, ptr, cmp_ptr, xchg, success, failure) \
            (((PzAtomic(type)*)(ptr)).compare_exchange_strong(cmp_ptr, xchg, success, failure))

    #else
        #include <stdatomic.h>

        #define PzAtomic(type) _Atomic(type)

        #define PZ_ATOMIC_RELAXED memory_order_relaxed
        #define PZ_ATOMIC_ACQUIRE memory_order_acquire
        #define PZ_ATOMIC_RELEASE memory_order_release
        #define PZ_ATOMIC_ACQ_REL memory_order_acq_rel
        #define PZ_ATOMIC_SEQ_CST memory_order_seq_cst

        #define PzAtomicLoad(type, ptr, ordering) \
            (atomic_load_explicit(((PzAtomic(type)*)(ptr)), ordering))

        #define PzAtomicStore(type, ptr, value, ordering) \
            (atomic_stpre_explicit(((PzAtomic(type)*)(ptr)), value, ordering))

        #define PzAtomicSwap(type, ptr, value, ordering) \
            (atomic_exchange_explicit(((PzAtomic(type)*)(ptr)), value, ordering))

        #define PzAtomicFetchAdd(type, ptr, value, ordering) \
            (atomic_fetch_add_explicit(((PzAtomic(type)*)(ptr)), value, ordering))

        #define PzAtomicFetchSub(type, ptr, value, ordering) \
            (atomic_fetch_sub_explicit(((PzAtomic(type)*)(ptr)), value, ordering))

        #define PzAtomicCompareExchangeWeak(type, ptr, cmp_ptr, xchg, success, failure) \
            (atomic_compare_exchange_weak_explicit(((PzAtomic(type)*)(ptr)), cmp_ptr, xchg, success, failure))

        #define PzAtomicCompareExchangeStrong(type, ptr, cmp_ptr, xchg, success, failure) \
            (atomic_compare_exchange_strong_explicit(((PzAtomic(type)*)(ptr)), cmp_ptr, xchg, success, failure))

    #endif
#endif // PZ_ATOMIC_H