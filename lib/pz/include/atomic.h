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

        static PZ_INLINE uintptr_t PzAtomicLoad(const PzAtomicPtr* ptr) {
            return atomic_load_explicit(ptr, memory_order_relaxed);
        }

    #endif

#endif
