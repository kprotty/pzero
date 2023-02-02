#include "pz_pending.h"
#include "pz_event.h"

#ifdef _WIN32
    #define pz_thread_yield() Sleep(0)
#else
    #include <sched.h>
    #define pz_thread_yield() sched_yield()
#endif

pz_nonnull(1) static uintptr_t pz_pending_load_link(pz_pending_node* node) {
    uint32_t spin = 32;
    while (true) {
        void* link = atomic_load_explicit(&node->next, memory_order_acquire);
        if (pz_likely(link != NULL)) {
            return (uintptr_t)link;
        }

        if (pz_likely(pz_checked_sub(spin, 1, &spin))) {
            pz_atomic_spin_loop_hint();
            continue;
        }

        pz_thread_yield();

        pz_event event = PZ_EVENT_INIT;
        link = atomic_exchange_explicit(&node->next, (void*)&event, memory_order_acq_rel);
        if (pz_likely(link == NULL)) {
            pz_assert(pz_event_wait(&event, NULL));
            link = atomic_load_explicit(&node->next, memory_order_relaxed);
        }

        pz_assert(link != NULL);
        return (uintptr_t)link;
    }
}

pz_nonnull(1) static void pz_pending_store_link(pz_pending_node* node, uintptr_t tagged_node) {
    pz_assert(pz_pending_get_node(tagged_node) != NULL);

    void* link = atomic_exchange_explicit(&node->next, (void*)tagged_node, memory_order_acq_rel);
    if (link != NULL) {
        pz_event_set((pz_event*)link);
    }
}

pz_nonnull(1, 2) static bool pz_pending_acquire(
    pz_pending_queue* restrict queue,
    pz_pending_guard* restrict guard,
    uintptr_t tagged_node
) {
    pz_assert(pz_pending_get_node(tagged_node) != NULL);
    pz_assert(pz_pending_get_tag(tagged_node) != 0);

    uintptr_t tail = atomic_exchange_explicit(&queue->tail, tagged_node, memory_order_acq_rel);
    if (pz_likely((tail & 3) == 0)) {   
        guard->context = pz_pending_get_node(tail);
        guard->current = tagged_node;
        guard->last = tagged_node;
        return true;
    }

    pz_pending_node* prev = pz_pending_get_node(tail);
    pz_assert(prev != NULL);
    pz_pending_store_link(prev, tagged_node);
    return false;
}

pz_nonnull(1) static uintptr_t pz_pending_next(pz_pending_guard* guard) {
    uintptr_t current = guard->current;
    if (pz_unlikely(current == 0)) {
        return 0;
    }

    uintptr_t next = 0;
    if (pz_likely(current != guard->last)) {
        pz_pending_node* node = pz_pending_get_node(current);
        pz_assert(node != NULL);
        next = pz_pending_load_link(node);
    }

    guard->current = next;
    return current;
}

pz_nonnull(1, 2) static bool pz_pending_release(
    pz_pending_queue* restrict queue,
    pz_pending_guard* restrict guard
) {
    uintptr_t context = (uintptr_t)guard->context;
    pz_assert((context & 3) == 0);
    
    pz_pending_node* tail = pz_pending_get_node(guard->last);
    pz_assert(tail != NULL);

    bool released = atomic_compare_exchange_strong_explicit(
        &queue->tail,
        &guard->tail,
        context,
        memory_order_acq_rel,
        memory_order_acquire
    );

    if (pz_unlikely(!released)) {
        guard->current = pz_pending_load_link(tail);
        pz_assert(guard->current != 0);
    }

    return released;
}
