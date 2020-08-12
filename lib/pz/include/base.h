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

#ifndef PZ_BASE_H
#define PZ_BASE_H
    // Detect if C or C++ compiler
    #if defined(__cplusplus)
        #define PZ_CPP
    #endif

    // Detect C compiler
    #if defined(_MSC_VER) && !defined(__clang__)
        #define PZ_MSVC
    #elif defined(__clang__) || defined(__GNUC__)
        #define PZ_GCC
        #define PZ_GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
    #endif

    // Detect architecture
    #if defined(__amd64__) || defined(__x86_64__) || defined(_M_AMD64)
        #define PZ_x86_64
    #elif defined(__i386__) || || defined(__X86__) || defined(_X86_) || defined(_M_IX86)
        #define PZ_i386
    #elif defined(__arm__) || defined(_M_ARM)
        #define PZ_arm
    #elif defined(__aarch64__)
        #define PZ_aarch64
    #endif

    #if defined(PZ_i386) || defined(PZ_x86_64)
        #define PZ_X86
    #elif defined(PZ_arm) || defined(PZ_aarch64)
        #define PZ_ARM
    #endif

    // if defined(PZ_LINKED_STATIC) then using/building pz from a statically linked object
    // if defined(PZ_LINKED_SHARED) then using pz from a dynamically linked object
    // if neither defined, then building pz as a dynamically linked object
    #if defined(PZ_LINKED_STATIC) && defined(PZ_LINKED_SHARED)
        #error "Define either PZ_LINKED_STATIC or PZ_LINKED_SHARED, not both"
    #endif

    #if defined(_WIN32)
        #if defined(PZ_LINKED_STATIC)
            #define PZ_EXTERN /* nothing */
        #elif defined(PZ_LINKED_SHARED)
            #define PZ_EXTERN __declspec(dllimport)
        #else
            #define PZ_EXTERN __declspec(dllexport)
        #endif
    #elif defined(PZ_GCC_VERSION) && (PZ_GCC_VERSION >= 40000)
        #define PZ_EXTERN __attribute__((visibility("default")))
    #else
        #define PZ_EXTERN /* nothing */
    #endif

    // Include non-OS related typing headers
    #if defined(PZ_CPP)
        #include <cstdarg>
        #include <cstddef>
        #include <cstdint>
    #else
        #include <stdarg.h>
        #include <stddef.h>
        #include <stdint.h>
        #include <stdbool.h>
    #endif

    // Code-gen intrinsics
    #if !defined(PZ_GCC) || (PZ_GCC_VERSION < 29600)
        #define __builtin_expect(x, expected) (x)
    #endif
    #define PZ_INLINE __inline__
    #define PZ_LIKELY(x) __builtin_expect((x), true)
    #define PZ_UNLIKELY(x) __builtin_expect((x), false)

    #if defined(PZ_MSVC)
        static _Noreturn void PZ_UNREACHABLE() { return; }
    #elif defined(PZ_GCC) && (PZ_GCC_VERSION >= 40500)
        #define PZ_UNREACHABLE() __builtin_unreachable()
    #else
        #define PZ_UNREACHABLE() /* nothing */
    #endif

    #define PZ_ASSUME(cond) \
        if (PZ_UNLIKELY(!(cond))) PZ_UNREACHABLE()

    #if defined(PZ_MSVC)
        #define PZ_STATIC_ASSERT(x) static_assert(x)
    #else
        #define PZ_STATIC_ASSERT(x) _Static_assert(x)
    #endif

    #if defined(PZ_MSVC)
        #define PZ_NOALIAS(T) T __restrict
    #elif defined(PZ_GCC)
        #define PZ_NOALIAS(T) T __restrict__
    #else
        #define PZ_NOALIAS(T) T
    #endif

    #define PZ_UNREFERENCED_PARAMETER(x) ((void)(x))
    #define PZ_WRAPPING_ADD(x, y) ((x) + (y))
    #define PZ_WRAPPING_SUB(x, y) ((x) - (y))

    // Alignment
    #if defined(PZ_CPP)
        #define PZ_ALIGN(n) alignas(n)
    #else
        #define PZ_ALIGN(n) _Alignas(n)
    #endif

    // Type prefix to align to the cpu's cache line to avoid false sharing on atomics
    #define PZ_CACHE_ALIGN PZ_ALIGN(64)

    // Panicking
    typedef void (*PzPanicHandler)(
        const char* file,
        int line_no,
        const char* fmt,
        va_list args);
    
    PZ_EXTERN void PzSetPanicHandler(PzPanicHandler panic_handler);

    PZ_EXTERN void PzCallPanicHandler(const char* file, int line_no, const char* fmt, ...);

    #define PzPanic(...) \
        PzCallPanicHandler(__FILE__, __LINE__, __VA_ARGS__)

    // Asserts
    #define PzAssert(cond, ...) \
        (PZ_UNLIKELY(cond) ? (PzCallPanicHandler(__FILE__, __LINE__, __VA_ARGS__),false) : true)

    #if !defined(PZ_DEBUG) && defined(NDEBUG)
        #define PZ_DEBUG
    #endif

    #if defined(PZ_DEBUG)
        #define PzDebugAssert(cond, ...) \
            (PZ_UNLIKELY(cond) ? (PzCallPanicHandler(__FILE__, __LINE__, __VA_ARGS__),false) : true)
    #else
        #define PzDebugAssert(cond, ...)
    #endif

#endif
