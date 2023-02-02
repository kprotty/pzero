#pragma once

#include "pz_atomic.h"

typedef struct {
    pz_task* head;
    pz_task* tail;
} pz_batch;

pz_nonnull(1, 2) static void pz_batch_push(pz_batch* restrict batch, pz_task* restrict task);

pz_nonnull(1) static pz_task* pz_batch_pop(pz_batch* restrict batch);

typedef struct {
    _Alignas(PZ_ATOMIC_CACHE_LINE) _Atomic(uintptr_t) head;
    _Alignas(PZ_ATOMIC_CACHE_LINE) _Atomic(pz_task*) tail;
} pz_injector;

pz_nonnull(1, 2) static void pz_injector_push(pz_injector* restrict injector, pz_batch* restrict batch);

pz_nonnull(1) static bool pz_injector_pending(pz_injector* injector);

pz_nonnull(1) static pz_task* pz_injector_poll(pz_injector* injector);

enum { PZ_BUFFER_CAPACITY = 256 };

typedef struct {
    _Alignas(PZ_ATOMIC_CACHE_LINE) _Atomic(uint32_t) head;
    _Alignas(PZ_ATOMIC_CACHE_LINE) _Atomic(uint32_t) tail;
    _Atomic(pz_task*) array[PZ_BUFFER_CAPACITY];
} pz_buffer;

pz_nonnull(1, 2, 3) static void pz_buffer_push(pz_buffer* restrict buffer, pz_task* restrict task, pz_batch* restrict batch);

pz_nonnull(1) static pz_task* pz_buffer_pop(pz_buffer* buffer);

pz_nonnull(1, 2) static uintptr_t pz_buffer_steal(pz_buffer* restrict buffer, pz_buffer* restrict target);

pz_nonnull(1, 2) static uintptr_t pz_buffer_inject(pz_buffer* restrict buffer, pz_injector* restrict injector);
