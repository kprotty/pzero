#ifndef _PZ_BUILTIN_H
#define _PZ_BUILTIN_H

#include <pz.h>

// Supports only modern GCC compatible compilers
#ifndef __GNUC__
    #error "Only GCC-compatible compilers are currently supported"
#endif

// Attributes
#define PZ_COLD __attribute__((__cold__))
#define PZ_EXPORT __attribute__((dllexport))
#define PZ_ALIGNED(alignment) __attribute__((__aligned__(alignment)))

// Builtins
#define PZ_STATIC_ASSERT _Static_assert
#define PZ_LIKELY(x) __builtin_expect(!!(x), 1)
#define PZ_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define PZ_UNREACHABLE __builtin_unreachable
#define PZ_UNUSED(x) ((void)(x))

// Assertions
#if defined(NDEBUG)
    #define PZ_ASSERT(x) do { if (PZ_UNLIKELY(!(x))) PZ_UNREACHABLE(); } while (0)
#else
    #include <assert.h>
    #define PZ_ASSERT(x) assert(x)
#endif

// Arithmetic
#define PZ_WRAPPING_ADD(x, y) ((x) + (y))
#define PZ_WRAPPING_SUB(x, y) ((x) - (y))
#define PZ_WRAPPING_MUL(x, y) ((x) * (y))
#define PZ_OVERFLOW_ADD __builtin_add_overflow
#define PZ_OVERFLOW_SUB __builtin_sub_overflow

// Pointers & Aliasing
#define PZ_FIELD_OF(type, field, obj_ptr) \
    _Generic((obj_ptr), type*: (type*)((char*)(obj_ptr) + offsetof(type, field)))
#define PZ_OBJECT_OF(type, field, field_ptr) \
    _Generic((field_ptr), type*: (type*)((char*)(field_ptr) - offsetof(type, field)))

#endif // _PZ_BUILTIN_H
