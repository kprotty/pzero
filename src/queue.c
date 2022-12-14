#include "queue.h"

static void pz_queue_push(struct pz_queue* NOALIAS queue, struct pz_queue_node* NOALIAS node) {
    ASSUME(queue != NULL);
    ASSUME(node != NULL);

    size_t tail = atomic_load_uptr(&queue->tail);
    size_t head = atomic_load_uptr(&queue->head);

    while (true) {
        size_t size = WRAPPING_SUB(tail, head);
        ASSERT(size <= PZ_BUFFER_CAPACITY, "invalid queue head & tail");

        if (LIKELY(size < PZ_BUFFER_CAPACITY)) {
            atomic_store_uptr(&queue->buffer[tail % PZ_BUFFER_CAPACITY], UPTR(node));
            atomic_store_uptr_release(&queue->tail, WRAPPING_ADD(tail, 1));
            return;
        }

        size_t new_head = WRAPPING_ADD(head, size / 2);
        ASSUME(head != new_head);

        if (!atomic_cas_uptr_acquire(&queue->head, &head, new_head)) {
            continue;
        }

        struct pz_queue_list list = {0};
        for (; head != new_head; head = WRAPPING_ADD(head, 1)) {
            atomic_uptr* slot = &queue->buffer[head % PZ_BUFFER_CAPACITY];
            struct pz_queue_node* stolen = (struct pz_queue_node*)atomic_load_uptr(slot);
            pz_queue_list_push(&list, stolen);
        }

        pz_queue_list_push(&list, node);
        pz_queue_inject(queue, &list);
        return;
    }
}

static uintptr_t pz_queue_stole(struct pz_queue* queue, size_t tail, size_t new_tail) {
    ASSUME(queue != NULL);

    if (new_tail == tail) {
        return 0;
    }

    new_tail = WRAPPING_SUB(new_tail, 1);

    bool pushed = new_tail != tail;
    if (pushed) {
        atomic_store_uptr_release(&queue->tail, new_tail);
    }

    atomic_uptr* slot = &queue->buffer[new_tail % PZ_BUFFER_CAPACITY];
    struct pz_queue_node* stolen = (struct pz_queue_node*)atomic_load_uptr(slot);
    ASSERT(stolen != NULL, "queue returning stolen node without providing one");

    struct pz_queue_popped popped;
    popped.node = stolen;
    popped.pushed = pushed;
    return pz_queue_popped_encode(&popped);
}

static const uintptr_t INJECT_CONSUMING_BIT = UPTR(1);
static const uintptr_t INJECT_NODE_MASK = ~INJECT_CONSUMING_BIT;

static void pz_queue_inject(struct pz_queue* NOALIAS queue, const struct pz_queue_list* NOALIAS list) {
    ASSUME(queue != NULL);
    ASSUME(list != NULL);

    struct pz_queue_node* first = list->head;
    if (first == NULL) {
        return;
    }

    struct pz_queue_node* last = list->tail;
    ASSERT(last != NULL, "injecting invalid list with a head and no tail");
    ASSERT(*((struct pz_queue_node**)&last->next) == NULL, "injecting invalid list with tail->next not null");

    struct pz_queue_node* old_tail = (struct pz_queue_node*)atomic_swap_uptr_release(&queue->inject_head, UPTR(last));
    if (LIKELY(old_tail != NULL)) {
        atomic_store_uptr_release(&old_tail->next, UPTR(first));
        return;
    }

    uintptr_t old_head = atomic_fetch_add_uptr_release(&queue->inject_tail, UPTR(first));
    ASSERT((old_head & INJECT_NODE_MASK) == 0, "pushed to inject_tail when not empty");
}

static uintptr_t pz_queue_consume(struct pz_queue* queue, struct pz_queue* target) {
    ASSUME(queue != NULL);
    ASSUME(target != NULL);

    uintptr_t inject_head = atomic_load_uptr(&target->inject_head);
    do {
        if ((inject_head & INJECT_NODE_MASK) == 0) return 0; // nothing to consume.
        if ((inject_head & INJECT_CONSUMING_BIT) != 0) return 0; // there's another thread consuming.
    } while (!atomic_cas_uptr_acquire(&target->inject_head, &inject_head, INJECT_CONSUMING_BIT));

    struct pz_queue_node* inject_node = (struct pz_queue_node*)(inject_head & INJECT_NODE_MASK);
    ASSUME(inject_node != NULL);

    size_t tail = atomic_load_uptr(&queue->tail);
    size_t head = atomic_load_uptr(&queue->head);
    ASSERT(tail == head, "queue consuming from target when local buffer is not empty");

    size_t new_tail = tail;
    for (int i = 0; i < PZ_BUFFER_CAPACITY; i++) {
        if (UNLIKELY(inject_node == NULL)) {
            inject_head = atomic_swap_uptr_acquire(&target->inject_head, INJECT_CONSUMING_BIT);
            ASSERT((inject_head & INJECT_CONSUMING_BIT) != 0, "queue consuming without the CONSUME bit");
            inject_node = (struct pz_queue_node*)(inject_head & INJECT_NODE_MASK);
        }

        struct pz_queue_node* node = inject_node;
        if (UNLIKELY(node == NULL)) {
            break;
        }

        struct pz_queue_node* next = (struct pz_queue_node*)atomic_load_uptr_acquire(&node->next);
        if (UNLIKELY(next == NULL)) {
            uintptr_t inject_tail = UPTR(node);
            do {
                if (atomic_cas_uptr_acquire(&target->inject_tail, &inject_tail, 0)) break;
            } while (inject_tail == UPTR(node));

            if (UNLIKELY(inject_tail != UPTR(node))) {
                atomic_hint_yield();
                next = (struct pz_queue_node*)atomic_load_uptr_acquire(&node->next);
                if (UNLIKELY(next == NULL)) {
                    break;
                }
            }
        }

        inject_node = next;
        ASSUME(node != NULL);

        atomic_uptr* slot = &queue->buffer[new_tail % PZ_BUFFER_CAPACITY];
        atomic_store_uptr(slot, UPTR(node));
        new_tail = WRAPPING_ADD(new_tail, 1);
    }

    if (inject_guard != NULL) {
        inject_head = UPTR(inject_guard);
        ASSUME(inject_head & INJECT_CONSUMING_BIT == 0);
        atomic_store_uptr_release(&target->inject_head, inject_head);
    } else {
        inject_head = atomic_fetch_sub_uptr_release(&target->inject_head, INJECT_CONSUMING_BIT);
        ASSERT((inject_head & INJECT_CONSUMING_BIT) != 0, "queue consumed without the CONSUMING bit");
    }

    return pz_queue_stole(queue, tail, new_tail);
}

static uintptr_t pz_queue_pop(struct pz_queue* queue) {
    ASSUME(queue != 0);

    size_t tail = atomic_load_uptr(&queue->tail);
    size_t head = atomic_fetch_add_uptr_acquire(&queue->head, 1);

    size_t size = WRAPPING_SUB(tail, head);
    ASSERT(size <= PZ_BUFFER_CAPACITY, "invalid queue size when popping");

    if (UNLIKELY(size == 0)) {
        atomic_store_uptr(&queue->head, head);
        return pz_queue_consume(queue, queue);
    }

    atomic_uptr* slot = &queue->buffer[head % PZ_BUFFER_CAPACITY];
    struct pz_queue_node* stolen = (struct pz_queue_node*)atomic_load_uptr(slot);
    ASSERT(stolen != NULL, "queue popped an invalid node from local buffer");

    struct pz_queue_popped popped;
    popped.node = stolen;
    popped.pushed = false;
    return pz_queue_popped_encode(&popped);
}

static uintptr_t pz_queue_steal(struct pz_queue* queue, struct pz_queue* target, struct pz_random* NOALIAS rng) {
    ASSUME(queue != NULL);
    ASSUME(target != NULL);
    ASSUME(rng != NULL);

    while (true) {
        uintptr_t result = pz_queue_consume(queue, target);
        if (result != 0) {
            return result;
        }

        size_t target_head = atomic_load_uptr_acquire(&target->head);
        size_t target_tail = atomic_load_uptr_acquire(&target->tail);

        size_t target_size = WRAPPING_SUB(target_tail, target_head);
        if (((ssize_t)target_size) <= 0) {
            return 0;
        }

        size_t target_steal = target_size - (target_size / 2);
        if (UNLIKELY(target_steal > (PZ_BUFFER_CAPACITY / 2))) {
            atomic_hint_yield();
            continue;
        }

        size_t tail = atomic_load_uptr(&queue->tail);
        size_t head = atomic_load_uptr(&queue->head);
        ASSERT(queue != target, "queue stealing from it's own local buffer");
        ASSERT(tail == head, "queue stealing from target when local buffer is not empty");

        size_t new_tail = tail;
        size_t new_target_head = target_head;
        while (CHECKED_SUB(target_steal, 1, &target_steal)) {
            atomic_uptr* from_slot = &target->buffer[new_target_head % PZ_BUFFER_CAPACITY];
            new_target_head = WRAPPING_ADD(new_target_head, 1);

            atomic_uptr* to_slot = &queue->buffer[new_tail % PZ_BUFFER_CAPACITY];
            new_tail = WRAPPING_ADD(new_tail, 1);

            struct pz_queue_node* stolen = (struct pz_queue_node*)atomic_load_uptr(from_slot);
            atomic_store_uptr(to_slot, UPTR(stolen));
            ASSERT(stolen != NULL, "queue is stealing an invalid node from target");
        }

        if (!atomic_cas_uptr_acq_rel(&target->head, &target_head, new_target_head)) {
            atomic_hint_backoff(rng);
            continue;
        }

        return pz_queue_stole(queue, tail, new_tail);
    }
}
