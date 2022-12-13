#ifndef _PZ_QUEUE_H
#define _PZ_QUEUE_H

#include "atomic.h"
#include "random.h"

struct pz_queue_node {
    struct pz_queue_node* next;
};

struct pz_queue_list {
    struct pz_queue_node* head;
    struct pz_queue_node* tail;
};

static FORCE_INLINE void pz_queue_list_push(struct pz_queue_list* NOALIAS list, struct pz_queue_node* NOALIAS node) {
    ASSUME(list != NULL);
    ASSUME(node != NULL);

    struct pz_queue_node* tail = list->tail;
    struct pz_queue_node** link = LIKELY(tail != NULL) ? (&tail->next) : (&list->head);
    *link = node;

    list->tail = node;
    node->next = NULL;
}

enum { PZ_BUFFER_CAPACITY = 256 };

struct pz_queue {
    atomic_uptr head;
    atomic_uptr inject_head;
    uint8_t _cache_padding[ASSUMED_ATOMIC_CACHE_LINE - (sizeof(atomic_uptr) * 2)];

    atomic_uptr tail;
    atomic_uptr inject_tail;
    atomic_uptr buffer[PZ_BUFFER_CAPACITY];
};

static void pz_queue_push(struct pz_queue* NOALIAS queue, struct pz_queue_node* NOALIAS node);

static void pz_queue_inject(struct pz_queue* NOALIAS queue, const struct pz_queue_list* NOALIAS list);

struct pz_queue_popped {
    struct pz_queue_node* node;
    bool pushed;
};

static FORCE_INLINE uintptr_t pz_queue_popped_encode(struct pz_queue_popped* popped) {
    ASSUME(popped != NULL);
    uintptr_t ptr = (uintptr_t)(popped->node);
    ASSERT((ptr & UPTR(1)) == 0, "queue node is misaligned");
    return ptr | UPTR(popped->pushed);
}

static FORCE_INLINE void pz_queue_popped_decode(struct pz_queue_popped* popped, uintptr_t encoded) {
    ASSUME(popped != NULL);
    popped->node = (struct pz_queue_node*)(encoded & ~UPTR(1));
    popped->pushed = (encoded & UPTR(1)) != 0;
}

static uintptr_t pz_queue_pop(struct pz_queue* queue);

static uintptr_t pz_queue_steal(struct pz_queue* NOALIAS queue, struct pz_queue* NOALIAS target, struct pz_random* NOALIAS rng);

#endif // _PZ_QUEUE_H
