#include "queue.h"

static void pz_queue_push(struct pz_queue* NOALIAS queue, struct pz_queue_node* NOALIAS node) {
    ASSUME(queue != NULL);
    ASSUME(node != NULL);

    uintptr_t tail = atomic_load_uptr(&queue->tail);
    uintptr_t head = atomic_load_uptr(&queue->head);

    size_t size = WRAPPING_SUB(tail, head);
    ASSERT(size <= PZ_BUFFER_CAPACITY, "invalid queue head & tail");

    if (UNLIKELY(size == PZ_BUFFER_CAPACITY)) {
        size_t new_head = WRAPPING_ADD(head, size / 2);
        ASSUME(head != new_head);

        if (!atomic_cas_uptr_acquire(&queue->head, &head, new_head)) {
            size = WRAPPING_SUB(tail, head);
            goto push_to_tail;
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

push_to_tail:
    ASSERT(size < PZ_BUFFER_CAPACITY, "pushing to queue without a free slot");
    atomic_store_uptr(&queue->buffer[tail % PZ_BUFFER_CAPACITY], UPTR(node));
    atomic_store_uptr_release(&queue->tail, WRAPPING_ADD(tail, 1));
}

static const uintptr_t INJECT_TAIL_CONSUMING_BIT = UPTR(1);
static const uintptr_t INJECT_TAIL_NODE_MASK = ~INJECT_TAIL_CONSUMING_BIT;

static void pz_queue_inject(struct pz_queue* NOALIAS queue, const struct pz_queue_list* NOALIAS list) {
    ASSUME(queue != NULL);
    ASSUME(list != NULL);

    struct pz_queue_node* first = list->head;
    if (first == NULL) {
        return;
    }

    struct pz_queue_node* last = list->tail;
    ASSERT(last != NULL, "injecting invalid list with a head and no tail");
    ASSERT(last->next == NULL, "injecting invalid list with tail->next not null");

    struct pz_queue_node* old_tail = (struct pz_queue_node*)atomic_swap_uptr_release(&queue->inject_head, UPTR(last));
    if (LIKELY(old_tail != NULL)) {
        atomic_store_uptr_release((atomic_uptr*)(&old_tail->next), UPTR(first));
        return;
    }

    uintptr_t old_head = atomic_fetch_add_uptr_release(&queue->inject_tail, UPTR(first));
    ASSERT((old_head & INJECT_TAIL_NODE_MASK) == 0, "pushed to inject_tail when not empty");
}

static uintptr_t pz_queue_consume(struct pz_queue* queue, struct pz_queue* target) {
    
}

static uintptr_t pz_queue_pop(struct pz_queue* queue) {
    (void)queue;
    return 0;
}

static uintptr_t pz_queue_steal(struct pz_queue* NOALIAS queue, struct pz_queue* NOALIAS target, struct pz_random* NOALIAS rng) {
    ASSUME(queue != NULL);
    ASSUME(target != NULL);
    ASSUME(rng != NULL);

    return 0;
}
