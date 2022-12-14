#ifndef _PZ_BUILTIN_H
#define _PZ_BUILTIN_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>

#ifndef __GNUC__
    #error "A GNUC compatible compiler is required"
#endif

#if defined(_WIN32)
    #include <Windows.h>
    #define OS_WINDOWS
#elif defined(__APPLE__)
    #define OS_DARWIN
#elif defined(__linux__)
    #define OS_LINUX
#elif defined(__DragonFly__)
    #define OS_DRAGONFLY
#elif defined(__FreeBSD__)
    #define OS_FREEBSD
#elif defined(__NetBSD__)
    #define OS_NETBSD
#elif defined(__OpenBSD__)
    #define OS_OPENBSD
#else
    #error "target OS is not currently supported"
#endif

#if defined(__x86_64__)
    #define ARCH_X64
#elif defined(__i386__)
    #define ARCH_X86
#elif defined(__aarch64__)
    #define ARCH_ARM64
#elif defined(__arm__) || defined(__thumb__)
    #define ARCH_ARM
#else
    #error "target ARCH is not currently supported"
#endif

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define UNREACHABLE __builtin_unreachable
#define STATIC_ASSERT _Static_assert
#define FORCE_INLINE inline __attribute__((__always_inline__))
#define NOALIAS restrict

#define ASSERT(cond, msg) assert((cond) && (msg))
#ifdef NDEBUG
    #define ASSUME(cond) ASSERT((cond), "assumption invalidated")
#else
    #define ASSUME(cond) do { if (UNLIKELY(!(cond))) UNREACHABLE(); } while (0)
#endif

#define UPTR(x) ((uintptr_t)(x))
#define WRAPPING_ADD(x, y) ((x) + (y))
#define WRAPPING_SUB(x, y) ((x) - (y))
#define CHECKED_ADD __builtin_add_overflow 
#define CHECKED_SUB __builtin_sub_overflow

#endif // _PZ_BUILTIN_H
