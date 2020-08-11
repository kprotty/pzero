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

    #include "base.h"
    #if UINTPTR_MAX != SIZE_MAX
        #error "Pz only supported on platforms with pointer-width atomics"
    #endif

    #if defined(PZ_MSVC)
        #error "Atomic definitions for MSVC not implemented yet"

    #else
        #include <stdatomic.h>

        typedef _Atomic(uintptr_t) PzAtomicPtr;

        /// Equivalent to LLVM atomic load with monotonic memory ordering
        static PZ_INLINE uintptr_t PzAtomicLoad(const PzAtomicPtr* ptr) {
            return atomic_load_explicit(ptr, memory_order_relaxed);
        }

        /// Equivalent to LLVM atomic load with Unordered memory ordering
        static PZ_INLINE uintptr_t PzAtomicLoadUnordered(const PzAtomicPtr* ptr) {
            #if defined(PZ_X86) || defined(PZ_ARM)
                return *ptr;
            #else
                return PzAtomicLoad(ptr);
            #endif
        }

        /// Equivalent to LLVM atomic store with monotonic memory ordering
        static PZ_INLINE void PzAtomicStore(PzAtomicPtr* ptr, uintptr_t value) {
            atomic_store_explicit(ptr, value, memory_order_relaxed);
        }

        /// Equivalent to LLVM atomic store with release memory ordering
        static PZ_INLINE void PzAtomicStoreRelease(PzAtomicPtr* ptr, uintptr_t value) {
            atomic_store_explicit(ptr, value, memory_order_release);
        }

        /// Equivalent to LLVM atomic store with Unordered memory ordering
        static PZ_INLINE void PzAtomicStoreUnordered(PzAtomicPtr* ptr, uintptr_t value) {
            #if defined(PZ_X86) || defined(PZ_ARM)
                *ptr = value;
            #else
                PzAtomicStore(ptr, value);
            #endif
        }

        /// Equivalent to LLVM atomic cmpxchgWeak with Acquire memory ordering on success
        static PZ_INLINE bool PzAtomicCasAcquire(PzAtomicPtr* ptr, uintptr_t* cmp, uintptr_t xchg) {
            return atomic_compare_exchange_weak_explicit(ptr, cmp, xchg, memory_order_acquire, memory_order_relaxed);
        }

        /// Equivalent to LLVM atomic cmpxchgWeak with Release memory ordering on success
        static PZ_INLINE bool PzAtomicCasRelease(PzAtomicPtr* ptr, uintptr_t* cmp, uintptr_t xchg) {
            return atomic_compare_exchange_weak_explicit(ptr, cmp, xchg, memory_order_release, memory_order_relaxed);
        }

    #endif

#endif
