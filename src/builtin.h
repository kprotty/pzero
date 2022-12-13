#ifndef _PZ_BUILTIN_H
#define _PZ_BUILTIN_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>

#ifndef __GNUC__
    #error "A GNUC compatible compiler is required"
#endif

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define UNREACHABLE __builtin_unreachable
#define STATIC_ASSERT _Static_assert
#define FORCE_INLINE inline __attribute__((__always_inline__))
#define NOALIAS restrict

#define ASSERT(cond, msg) assert((cond) && (msg))
#ifdef NDEBUG
    #define ASSUME(cond) ASSERT((cond), "reached unreachable")
#else
    #define ASSUME(cond) do { if (!(cond)) UNREACHABLE(); } while (0)
#endif

#define UPTR(x) ((uintptr_t)(x))
#define WRAPPING_ADD(x, y) ((x) + (y))
#define WRAPPING_SUB(x, y) ((x) - (y))
#define CHECKED_ADD __builtin_add_overflow 
#define CHECKED_SUB __builtin_sub_overflow

#endif // _PZ_BUILTIN_H
