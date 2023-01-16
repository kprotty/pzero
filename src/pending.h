#ifndef _PZ_PENDING_H
#define _PZ_PENDING_H

#include "builtin.h"

#define PZ_PENDING_MASK ((uintptr_t)3)
enum PZ_PENDING_TAG {
    PZ_PENDING_IDLE = 0,
    PZ_PENDING_INSERT = 1,
    PZ_PENDING_REMOVE = 2,
    PZ_PENDING_UPDATE = 3,
};

struct PZ_ALIGNED(4) pz_pending_node {
    _Atomic(uintptr_t) next;
};

struct pz_pending_queue {
    _Atomic(uintptr_t) tail;
};

struct pz_pending_guard {
    uintptr_t current;
    uintptr_t last;
};

PZ_NONNULL(1, 2, 3)
static uintptr_t pz_pending_acquire(
    struct pz_pending_queue* restrict queue,
    struct pz_pending_guard* restrict guard,
    struct pz_pending_node* restrict node,
    enum PZ_PENDING_TAG tag);

PZ_NONNULL(1)
static uintptr_t pz_pending_next(struct pz_pending_guard* guard);

PZ_NONNULL(1, 2)
static bool pz_pending_release(
    struct pz_pending_queue* restrict queue,
    struct pz_pending_guard* restrict guard,
    uintptr_t context);

#endif // _PZ_PENDING_H
