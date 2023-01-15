#include "queue.h"

#define PZ_QUEUE_INJECT_CONSUMING ((uintptr_t)1)

PZ_NONNULL(1)
static bool pz_queue_injector_pending(const struct pz_queue_injector* injector) {
    uintptr_t head = atomic_load_explicit(&injector->head, memory_order_relaxed);
    return (head & ~PZ_QUEUE_INJECT_CONSUMING) != 0;
}

PZ_NONNULL(1, 2)
static void pz_queue_injector_push(
    struct pz_queue_injector* restrict injector,
    const struct pz_queue_batch* batch)
{
    struct pz_queue_node* front = batch->head;
    if (front == NULL) {
        return;
    }

    struct pz_queue_node* back = batch->tail;
    PZ_ASSERT(back != NULL);
    PZ_ASSERT(atomic_load_explicit(&back->next, memory_order_relaxed) == NULL);

    struct pz_queue_node* tail = atomic_exchange_explicit(&injector->tail, back, memory_order_release);
    if (PZ_LIKELY(tail != NULL)) {
        atomic_store_explicit(&tail->next, front, memory_order_release);
        return;
    }

    uintptr_t head = atomic_fetch_add_explicit(&injector->head, (uintptr_t)front, memory_order_release);
    PZ_ASSERT((head & ~PZ_QUEUE_INJECT_CONSUMING) == 0);
}

PZ_NONNULL(1)
static inline struct pz_queue_node* pz_queue_injector_acquire(struct pz_queue_injector* injector) {
    uintptr_t head = atomic_load_explicit(&injector->head, memory_order_relaxed);
    while (true) {
        struct pz_queue_node* consumer = (struct pz_queue_node*)(head & ~PZ_QUEUE_INJECT_CONSUMING);
        if (PZ_LIKELY(consumer == NULL)) {
            return NULL;
        }

        if (PZ_UNLIKELY((head & PZ_QUEUE_INJECT_CONSUMING) != 0)) {
            return NULL;
        }

        if (PZ_LIKELY(atomic_compare_exchange_weak_explicit(
            &injector->head, &head, PZ_QUEUE_INJECT_CONSUMING,
            memory_order_acquire,
            memory_order_relaxed
        ))) return consumer;
    }
}

PZ_NONNULL(1, 2)
static inline struct pz_queue_node* pz_queue_injector_pop(
    struct pz_queue_injector* restrict injector,
    struct pz_queue_node** restrict consumer)
{
    struct pz_queue_node* front = *consumer;
    if (PZ_UNLIKELY(front == NULL)) {
        uintptr_t head = atomic_load_explicit(&injector->head, memory_order_acquire);
        PZ_ASSERT((head & PZ_QUEUE_INJECT_CONSUMING) != 0);

        front = (struct pz_queue_node*)(head & ~PZ_QUEUE_INJECT_CONSUMING);
        if (PZ_UNLIKELY(front == NULL)) {
            return NULL;
        }

        *consumer = front;
        atomic_store_explicit(&injector->head, PZ_QUEUE_INJECT_CONSUMING, memory_order_relaxed);
    }

    struct pz_queue_node* next = atomic_load_explicit(&front->next, memory_order_acquire);
    if (PZ_UNLIKELY(next == NULL)) {
        struct pz_queue_node* tail = front;
        if (PZ_UNLIKELY(atomic_compare_exchange_strong_explicit(
            &injector->tail, &tail, NULL,
            memory_order_acq_rel,
            memory_order_acquire
        ))) {
            next = (struct pz_queue_node*)pz_atomic_mwait((_Atomic(void*)*)&front->next);
            if (PZ_UNLIKELY(next == NULL)) {
                return NULL;
            }
        }
    }

    *consumer = next;
    return front;
}

PZ_NONNULL(1)
static inline void pz_queue_injector_release(
    struct pz_queue_injector* restrict injector,
    struct pz_queue_node* restrict consumer)
{
    if (PZ_UNLIKELY(consumer != NULL)) {
        atomic_store_explicit(&injector->head, (uintptr_t)consumer, memory_order_release);
        return;
    }

    uintptr_t head = atomic_fetch_sub_explicit(&injector->head, PZ_QUEUE_INJECT_CONSUMING, memory_order_release);
    PZ_ASSERT((head & PZ_QUEUE_INJECT_CONSUMING) != 0);
}

PZ_NONNULL(1, 2, 3)
static void pz_queue_buffer_push(
    struct pz_queue_buffer* restrict buffer,
    struct pz_queue_node* restrict node,
    struct pz_queue_batch* restrict overflow)
{
    uint32_t head = atomic_load_explicit(&buffer->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&buffer->tail, memory_order_relaxed);

    uint32_t size = PZ_WRAPPING_SUB(tail, head);
    PZ_ASSERT(size <= PZ_QUEUE_BUFFER_CAPACITY);

    if (size == PZ_QUEUE_BUFFER_CAPACITY) {
        uint32_t migrate = size / 2;
        PZ_ASSERT(migrate > 0);

        if (PZ_LIKELY(atomic_compare_exchange_strong_explicit(
            &buffer->head, &head, PZ_WRAPPING_ADD(head, migrate),
            memory_order_acquire,
            memory_order_relaxed
        ))) {
            while (PZ_UNLIKELY(PZ_OVERFLOW_SUB(migrate, 1, &migrate))) {
                _Atomic(struct pz_queue_node*)* slot = &buffer->array[head % PZ_QUEUE_BUFFER_CAPACITY];
                head = PZ_WRAPPING_ADD(head, 1);

                struct pz_queue_node* stolen = atomic_load_explicit(slot, memory_order_relaxed);
                PZ_ASSERT(stolen != NULL);
                pz_queue_batch_push(overflow, stolen);
            }
        }

        size = PZ_WRAPPING_SUB(tail, head);
        PZ_ASSERT(size <= PZ_QUEUE_BUFFER_CAPACITY);
    }

    PZ_ASSERT(size < PZ_QUEUE_BUFFER_CAPACITY);
    atomic_store_explicit(&buffer->array[tail % PZ_QUEUE_BUFFER_CAPACITY], node, memory_order_relaxed);
    atomic_store_explicit(&buffer->tail, PZ_WRAPPING_ADD(tail, 1), memory_order_release);
}

PZ_NONNULL(1)
static struct pz_queue_node* pz_queue_buffer_pop(struct pz_queue_buffer* buffer)
{
    uint32_t head = atomic_fetch_add_explicit(&buffer->head, 1, memory_order_acquire);
    uint32_t tail = atomic_load_explicit(&buffer->tail, memory_order_relaxed);

    uint32_t size = PZ_WRAPPING_SUB(tail, head);
    PZ_ASSERT(size <= PZ_QUEUE_BUFFER_CAPACITY);

    if (PZ_UNLIKELY(size == 0)) {
        atomic_store_explicit(&buffer->head, head, memory_order_relaxed);
        return NULL;
    }

    _Atomic(struct pz_queue_node*)* slot = &buffer->array[head % PZ_QUEUE_BUFFER_CAPACITY];
    struct pz_queue_node* stolen = atomic_load_explicit(slot, memory_order_relaxed);
    PZ_ASSERT(stolen != NULL);
    return stolen;
}

PZ_NONNULL(1)
static inline uintptr_t pz_queue_buffer_commit(struct pz_queue_buffer* buffer, uint32_t tail, uint32_t pushed) {
    if (PZ_UNLIKELY(!PZ_OVERFLOW_SUB(pushed, 1, &pushed))) {
        return 0;
    }
    
    uint32_t new_tail = PZ_WRAPPING_ADD(tail, pushed);
    _Atomic(struct pz_queue_node*)* slot = &buffer->array[new_tail % PZ_QUEUE_BUFFER_CAPACITY];
    struct pz_queue_node* stolen = atomic_load_explicit(slot, memory_order_relaxed);
    PZ_ASSERT(stolen != NULL);

    bool submitted = pushed > 0;
    if (PZ_UNLIKELY(submitted)) {
        atomic_store_explicit(&buffer->tail, new_tail, memory_order_release);
    }

    PZ_ASSERT((((uintptr_t)stolen) & 1) == 0);
    return ((uintptr_t)stolen) | ((uintptr_t)((int)submitted)); 
}

PZ_NONNULL(1, 2)
static uintptr_t pz_queue_buffer_steal(
    struct pz_queue_buffer* restrict buffer,
    struct pz_queue_buffer* restrict target)
{
    while (true) {
        uint32_t target_head = atomic_load_explicit(&target->head, memory_order_acquire);
        uint32_t target_tail = atomic_load_explicit(&target->tail, memory_order_acquire);

        uint32_t target_size = PZ_WRAPPING_SUB(target_tail, target_head);
        if (PZ_LIKELY(((int32_t)target_size) <= 0)) {
            return 0;
        }

        uint32_t target_steal = target_size - (target_size / 2);
        if (PZ_UNLIKELY(target_steal > (PZ_QUEUE_BUFFER_CAPACITY / 2))) {
            pz_atomic_yield();
            continue;
        }

        uint32_t head = atomic_load_explicit(&buffer->head, memory_order_relaxed);
        uint32_t tail = atomic_load_explicit(&buffer->tail, memory_order_relaxed);
        PZ_ASSERT(head == tail);

        uint32_t pushed = 0;
        while (PZ_UNLIKELY(PZ_OVERFLOW_SUB(target_steal, 1, &target_steal))) {
            uint32_t src_index = PZ_WRAPPING_ADD(target_head, pushed) % PZ_QUEUE_BUFFER_CAPACITY;
            uint32_t dst_index = PZ_WRAPPING_ADD(tail, pushed) % PZ_QUEUE_BUFFER_CAPACITY;
            pushed += 1;

            struct pz_queue_node* stolen = atomic_load_explicit(&target->array[src_index], memory_order_relaxed);
            atomic_store_explicit(&buffer->array[dst_index], stolen, memory_order_relaxed);
            PZ_ASSERT(stolen != NULL);
        }

        if (PZ_UNLIKELY(!atomic_compare_exchange_strong_explicit(
            &target->head, &target_head, PZ_WRAPPING_ADD(target_head, pushed),
            memory_order_acq_rel,
            memory_order_relaxed
        ))) {
            pz_atomic_backoff();
            continue;
        }

        return pz_queue_buffer_commit(buffer, tail, pushed);
    }
}

PZ_NONNULL(1, 2)
static uintptr_t pz_queue_buffer_inject(
    struct pz_queue_buffer* restrict buffer,
    struct pz_queue_injector* restrict injector)
{
    struct pz_queue_node* consumer = pz_queue_injector_acquire(injector);
    if (PZ_LIKELY(consumer == NULL)) {
        return 0;
    }

    uint32_t head = atomic_load_explicit(&buffer->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&buffer->tail, memory_order_relaxed);
    PZ_ASSERT(head == tail);

    uint32_t pushed = 0;
    while (PZ_LIKELY(pushed < PZ_QUEUE_BUFFER_CAPACITY)) {
        struct pz_queue_node* stolen = pz_queue_injector_pop(injector, &consumer);
        if (PZ_UNLIKELY(stolen == NULL)) {
            break;
        }

        uint32_t index = PZ_WRAPPING_ADD(tail, pushed) % PZ_QUEUE_BUFFER_CAPACITY;
        atomic_store_explicit(&buffer->array[index], stolen, memory_order_relaxed);
        pushed += 1;
    }

    pz_queue_injector_release(injector, consumer);
    return pz_queue_buffer_commit(buffer, tail, pushed);
}
