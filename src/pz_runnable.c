#include "pz_runnable.h"

#define PZ_INJECTOR_CONSUMING ((uintptr_t)1)

PZ_NONNULL(1)
static bool pz_injector_pending(const pz_injector* injector) {
    uintptr_t head = atomic_load_explicit(&injector->head, memory_order_relaxed);
    if ((head & ~PZ_INJECTOR_CONSUMING) == 0) return false; // empty
    if ((head & PZ_INJECTOR_CONSUMING) != 0) return false; // consuming
    return true;
}

PZ_NONNULL(1, 2)
static void pz_injector_push(pz_injector* restrict injector, const pz_batch* restrict batch) {
    #error TODO
}

PZ_NONNULL(1, 2, 3)
static void pz_buffer_push(pz_buffer* restrict buffer, pz_runnable* restrict node, pz_batch* restrict batch) {
    #error TODO
}

PZ_NONNULL(1)
static pz_runnable* pz_buffer_pop(pz_buffer* buffer) {
    #error TODO
}

PZ_NONNULL(1, 2, 3)
static uintptr_t pz_buffer_steal(pz_buffer* restrict buffer, pz_buffer* restrict target, uint32_t* restrict prng) {
    #error TODO
}

PZ_NONNULL(1, 2)
static uintptr_t pz_buffer_inject(pz_buffer* restrict buffer, pz_injector* restrict injector) {
    #error TODO
}