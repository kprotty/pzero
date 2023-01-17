#ifndef _PZ_BUILTIN_H
#define _PZ_BUILTIN_H

#include "pz.h"

#ifndef __GNUC__
#error Only GCC-compatible compilers are currently supported.
#endif

#define PZ_COLD __attribute__((__cold__))
#define PZ_ALIGNED(alignment) __attribute__((__aligned__(alignment)))
#define PZ_NONNULL(...) __attribute__((__nonnull__(__VA_ARGS__)))

#define PZ_OVERFLOW_ADD __builtin_add_overflow
#define PZ_OVERFLOW_SUB __builtin_sub_overflow
#define PZ_OVERFLOW_MUL __builtin_mul_overflow

#define PZ_LIKELY(cond) __builtin_expect((cond), 1)
#define PZ_UNLIKELY(cond) __builtin_expect((cond), 0)

#define PZ_FIELD_OF(type, field, obj_ptr) \
    _Generic((obj_ptr), type*: (type*)((char*)(obj_ptr) + offsetof(type, field)))
#define PZ_OBJECT_OF(type, field, field_ptr) \
    _Generic((field_ptr), type*: (type*)((char*)(field_ptr) - offsetof(type, field)))

#if defined(NDEBUG)
    #define PZ_ASSERT(x) do { if (PZ_UNLIKELY(!(x))) __builtin_unreachable(); } while (0)
#else
    #include <assert.h>
    #define PZ_ASSERT assert
#endif

typedef 

#endif // _PZ_BUILTIN_H