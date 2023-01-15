#ifndef _PZ_QUEUE_H
#define _PZ_QUEUE_H

#include "atomic.h"

struct PZ_ALIGNED(2) pz_queue_node {
    _Atomic(struct pz_queue_node*) next;
};

struct pz_queue_batch {
    struct pz_queue_node* head;
    struct pz_queue_node* tail;
};

PZ_NONNULL(1, 2)
static inline void pz_queue_batch_push(
    struct pz_queue_batch* restrict batch,
    struct pz_queue_node* restrict node)
{
    struct pz_queue_node* tail = batch->tail;
    batch->tail = node;
    node->next = NULL;

    if (tail != NULL) {
        atomic_store_explicit(&tail->next, node, memory_order_relaxed);
    } else {
        batch->head = node;
    }
}

PZ_NONNULL(1)
static inline struct pz_queue_node* pz_queue_batch_pop(struct pz_queue_batch* batch) {
    struct pz_queue_node* node = batch->head;
    if (PZ_LIKELY(node != NULL)) {
        batch->head = atomic_load_explicit(&node->next, memory_order_relaxed);
        if (batch->head == NULL) {
            batch->tail = NULL;
        }
    }
    return node;
}

struct pz_queue_injector {
    _Alignas(PZ_ATOMIC_CACHE_LINE) _Atomic(uintptr_t) head;
    _Alignas(PZ_ATOMIC_CACHE_LINE) _Atomic(struct pz_queue_node*) tail;
};

PZ_NONNULL(1, 2)
static void pz_queue_injector_push(
    struct pz_queue_injector* restrict injector,
    const struct pz_queue_batch* batch);

PZ_NONNULL(1)
static bool pz_queue_injector_pending(const struct pz_queue_injector* injector);

enum { PZ_QUEUE_BUFFER_CAPACITY = 128 };

struct pz_queue_buffer {
    _Alignas(PZ_ATOMIC_CACHE_LINE) _Atomic(uint32_t) head;
    _Alignas(PZ_ATOMIC_CACHE_LINE) _Atomic(uint32_t) tail;
    _Atomic(struct pz_queue_node*) array[PZ_QUEUE_BUFFER_CAPACITY];
};

PZ_NONNULL(1, 2, 3)
static void pz_queue_buffer_push(
    struct pz_queue_buffer* restrict buffer,
    struct pz_queue_node* restrict node,
    struct pz_queue_batch* restrict overflow);

PZ_NONNULL(1)
static struct pz_queue_node* pz_queue_buffer_pop(struct pz_queue_buffer* buffer);

PZ_NONNULL(1, 2)
static uintptr_t pz_queue_buffer_steal(
    struct pz_queue_buffer* restrict buffer,
    struct pz_queue_buffer* restrict target);

PZ_NONNULL(1, 2)
static uintptr_t pz_queue_buffer_inject(
    struct pz_queue_buffer* restrict buffer,
    struct pz_queue_injector* restrict injector);  

#endif // _PZ_QUEUE_H
