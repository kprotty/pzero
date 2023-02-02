#pragma once

#include "pz.h"

#define pz_cold __attribute__((__cold__))
#define pz_aligned(n) __attribute__((__aligned__(n)))
#define pz_nonnull(...) __attribute__((__nonnull__(__VA_ARGS__)))

#define pz_likely(cond) __builtin_expect((cond), 1)
#define pz_unlikely(cond) __builtin_expect((cond), 0)

#define pz_checked_add(x, y, res) (!__builtin_add_overflow((x), (y), (res)))
#define pz_checked_sub(x, y, res) (!__builtin_sub_overflow((x), (y), (res)))
#define pz_checked_mul(x, y, res) (!__builtin_mul_overflow((x), (y), (res)))

#define pz_static_assert _Static_assert
#ifdef NDEBUG
    #define pz_assert(cond) do { if (pz_unlikely(!(cond))) __builtin_unreachable(); } while (0)
#else
    #include <assert.h>
    #define pz_assert assert
#endif
