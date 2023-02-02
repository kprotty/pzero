#pragma once

#include "pz_atomic.h"

typedef enum {
    PZ_PENDING_INSERT = 1,
    PZ_PENDING_REMOVE = 2,
    PZ_PENDING_UPDATE = 3,
} PZ_PENDING_TAG;

typedef struct {
    _Atomic(void*) next;
} pz_aligned(4) pz_pending_node;

static inline uintptr_t pz_pending_tagged(pz_pending_node* node, PZ_PENDING_TAG tag) {
    pz_assert(tag > 0);
    pz_assert(tag <= PZ_PENDING_UPDATE);
    pz_assert((((uintptr_t)node) & 3) == 0);
    return ((uintptr_t)node) | ((uintptr_t)tag);
}

static inline pz_pending_node* pz_pending_get_node(uintptr_t tagged_node) {
    return (pz_pending_node*)(tagged_node & ~((uintptr_t)3));
}

static inline PZ_PENDING_TAG pz_pending_get_tag(uintptr_t tagged_node) {
    pz_assert((tagged_node & 3) != 0);
    return (PZ_PENDING_TAG)(tagged_node & 3);
}

typedef struct {
    _Atomic(void*) tail;
} pz_pending_queue;

typedef struct {
    uintptr_t last;
    uintptr_t current;
    pz_pending_node* context;
} pz_pending_guard;

pz_nonnull(1, 2) static bool pz_pending_acquire(
    pz_pending_queue* restrict queue,
    pz_pending_guard* restrict guard,
    uintptr_t tagged_node
);

pz_nonnull(1) static uintptr_t pz_pending_next(pz_pending_guard* guard);

pz_nonnull(1, 2) static bool pz_pending_release(
    pz_pending_queue* restrict queue,
    pz_pending_guard* restrict guard
);
