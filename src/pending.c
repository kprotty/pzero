#include "pending.h"
#include "parker.h"

PZ_NONNULL(1)
static uintptr_t pz_pending_load_link(struct pz_pending_node* node) {
    uintptr_t link = (uintptr_t)pz_atomic_mwait((_Atomic(void*)*)&node->next);
    if (PZ_LIKELY(link != 0)) {
        return link;
    }


}

PZ_NONNULL(1)
static void pz_pending_store_link(struct pz_pending_node* restrict node, uintptr_t link) {

}

PZ_NONNULL(1, 2, 3)
static uintptr_t pz_pending_acquire(
    struct pz_pending_queue* restrict queue,
    struct pz_pending_guard* restrict guard,
    struct pz_pending_node* restrict node,
    PZ_PENDING_TAG tag)
{
    PZ_ASSERT((tag & PZ_PENDING_MASK) == tag);
    PZ_ASSERT(((uintptr_t)node & ~PZ_PENDING_MASK) == 0);

    uintptr_t new_tail = ((uintptr_t)node) | (uintptr_t)tag;
    uintptr_t old_tail = atomic_exchange_explicit(&queue->tail, new_tail, memory_order_acq_rel);

    if (PZ_UNLIKELY((old_tail & PZ_PENDING_MASK) != PZ_PENDING_IDLE)) {
        struct pz_pending_node* prev_node = (struct pz_pending_node*)(old_tail & ~PZ_PENDING_MASK);
        PZ_ASSERT(prev_node != NULL);
        pz_pending_store_link(prev_node, new_tail);
        return 0;
    }

    guard->last = 0;
    guard->current = new_tail;
    return old_tail | 1;
}

PZ_NONNULL(1)
static uintptr_t pz_pending_next(struct pz_pending_guard* guard)
{
    uintptr_t current = guard->current;
    if (PZ_UNLIKELY(current == 0)) {
        return 0;
    }

    struct pz_pending_node* node = (struct pz_pending_node*)(current & ~PZ_PENDING_MASK);
    PZ_ASSERT(node != NULL);

    guard->current = pz_pending_load_link(node);
    guard->last = current;
    return current;
}

PZ_NONNULL(1, 2)
static bool pz_pending_release(
    struct pz_pending_queue* restrict queue,
    struct pz_pending_guard* restrict guard,
    uintptr_t context)
{
    struct pz_pending_node* last_node = (struct pz_pending_node*)(guard->last & ~PZ_PENDING_MASK);
    PZ_ASSERT(last_node != NULL);

    if (PZ_LIKELY(atomic_compare_exchange_strong_explicit(
        &queue->tail, (uintptr_t)last_node, context,
        memory_order_acq_rel,
        memory_order_acquire
    ))) {
        return true;
    }

    guard->current = pz_pending_load_link(last_node);
    guard->last = 0;
    return false;
}