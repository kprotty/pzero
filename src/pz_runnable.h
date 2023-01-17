#ifndef _PZ_RUNNABLE_H
#define _PZ_RUNNABLE_H

#include "pz_builtin.h"
#include "pz_atomic.h"

typedef struct pz_runnable {
    _Atomic(struct pz_runnable*) next;
    pz_task_callback callback;
} pz_runnable;

typedef struct {
    pz_runnable* head;
    pz_runnable* tail;
} pz_batch;

PZ_NONNULL(1, 2)
static inline void pz_batch_push(pz_batch* restrict batch, pz_runnable* restrict node) {
    pz_runnable* tail = batch->tail;
    batch->tail = node;
    node->next = NULL;

    if (tail != NULL) {
        *((pz_runnable**)&tail->next) = node;
    } else {
        batch->head = node;
    }
}

PZ_NONNULL(1, 2)
static inline pz_runnable* pz_batch_push(pz_batch* batch) {
    pz_runnable* node = batch->head;
    if (PZ_UNLIKELY(node != NULL)) {
        batch->head = *((pz_runnable**)&node->next);
        if (batch->head == NULL) {
            batch->tail = NULL;
        }
    }
    return node;
}

typedef struct {
    alignas(PZ_ATOMIC_CACHE_LINE) _Atomic(uintptr_t) head;
    alignas(PZ_ATOMIC_CACHE_LINE) _Atomic(pz_runnable*) tail;
} pz_injector;

PZ_NONNULL(1)
static bool pz_injector_pending(const pz_injector* injector);

PZ_NONNULL(1, 2)
static void pz_injector_push(pz_injector* restrict injector, const pz_batch* restrict batch);

enum { PZ_BUFFER_CAPACITY = 128 };

typedef struct {
    alignas(PZ_ATOMIC_CACHE_LINE) _Atomic(uint32_t) head;
    alignas(PZ_ATOMIC_CACHE_LINE) _Atomic(uint32_t) tail;
    _Atomic(pz_runnable*) array[PZ_BUFFER_CAPACITY];
} pz_buffer;

PZ_NONNULL(1, 2, 3)
static void pz_buffer_push(pz_buffer* restrict buffer, pz_runnable* restrict node, pz_batch* restrict batch);

PZ_NONNULL(1)
static pz_runnable* pz_buffer_pop(pz_buffer* buffer);

PZ_NONNULL(1, 2, 3)
static uintptr_t pz_buffer_steal(pz_buffer* restrict buffer, pz_buffer* restrict target, uint32_t* restrict prng);

PZ_NONNULL(1, 2)
static uintptr_t pz_buffer_inject(pz_buffer* restrict buffer, pz_injector* restrict injector);

#endif // _PZ_RUNNABLE_H