#include "pz_queue.h"

pz_nonnull(1, 2) static void pz_batch_push(pz_batch* restrict batch, pz_task* restrict task) {
    pz_task* tail = batch->tail;
    batch->tail = task;

    pz_task** prev = tail ? &(tail->next) : (&batch->head);
    *prev = task;

    task->next = NULL;
}

pz_nonnull(1) static pz_task* pz_batch_pop(pz_batch* restrict batch) {
    pz_task* task = batch->head;
    if (pz_unlikely(task != NULL)) {
        batch->head = task->next;
        if (batch->head == NULL) {
            batch->tail = NULL;
        }
    }

    return task;
}

static const uintptr_t pz_injector_consuming_bit = 1;

#define pz_injector_link(task) ((_Atomic(pz_task*)*)(&((task)->next)))

pz_nonnull(1, 2) static void pz_injector_push(pz_injector* restrict injector, pz_batch* restrict batch) {
    pz_task* front = batch->head;
    if (pz_likely(front == NULL)) {
        return;
    }

    pz_task* back = batch->tail;
    pz_assert(back != NULL);
    pz_assert(back->next == NULL);

    pz_task* tail = atomic_exchange_explicit(&injector->tail, back, memory_order_acq_rel);
    if (pz_unlikely(tail != NULL)) {
        atomic_store_explicit(pz_injector_link(tail), front, memory_order_release);
        return;
    }

    uintptr_t head = atomic_fetch_add_explicit(&injector->head, (uintptr_t)front, memory_order_release);
    pz_assert((((uintptr_t)front) & pz_injector_consuming_bit) == 0);
    pz_assert((head & ~pz_injector_consuming_bit) == 0);
}

pz_nonnull(1) static bool pz_injector_pending(pz_injector* injector) {
    uintptr_t head = atomic_load_explicit(&injector->head, memory_order_relaxed);
    bool has_tasks = (head & ~pz_injector_consuming_bit) != 0;
    bool can_acquire = (head & pz_injector_consuming_bit) == 0;
    return has_tasks && can_acquire;
}

pz_nonnull(1) static pz_task* pz_injector_acquire(pz_injector* injector) {
    uintptr_t head = atomic_load_explicit(&injector->head, memory_order_relaxed);
    while (true) {
        pz_task* consumer = (pz_task*)(head & ~pz_injector_consuming_bit);
        if (pz_likely(consumer == NULL)) {
            return NULL;
        }

        if (pz_unlikely((head & pz_injector_consuming_bit) != 0)) {
            return NULL;
        }

        if (pz_unlikely(atomic_compare_exchange_weak_explicit(
            &injector->head,
            &head,
            pz_injector_consuming_bit,
            memory_order_acquire,
            memory_order_relaxed
        ))) return consumer;
    }
}

pz_nonnull(1, 2) static pz_task* pz_injector_pop(pz_injector* restrict injector, pz_task** restrict consumer) {
    pz_task* front = *consumer;
    if (pz_unlikely(front == NULL)) {
        uintptr_t head = atomic_load_explicit(&injector->head, memory_order_acquire);
        pz_assert((head & pz_injector_consuming_bit) != 0);

        front = (pz_task*)(head & ~pz_injector_consuming_bit);
        if (pz_likely(front == NULL)) {
            return NULL;
        }

        *consumer = front;
        atomic_store_explicit(&injector->head, pz_injector_consuming_bit, memory_order_relaxed);
    }

    pz_task* next = atomic_load_explicit(pz_injector_link(front), memory_order_acquire);
    if (pz_unlikely(next == NULL)) {
        pz_task* tail = front;
        if (pz_unlikely(!atomic_compare_exchange_strong_explicit(
            &injector->tail,
            &tail,
            NULL,
            memory_order_acq_rel,
            memory_order_acquire
        ))) {
            next = atomic_load_explicit(pz_injector_link(front), memory_order_acquire);
            if (pz_unlikely(next == NULL)) {
                return NULL;
            }
        }
    }

    *consumer = next;
    return front;
}

pz_nonnull(1) static void pz_injector_release(pz_injector* restrict injector, pz_task* restrict consumer) {
    if (pz_unlikely(consumer != NULL)) {
        atomic_store_explicit(&injector->head, (uintptr_t)consumer, memory_order_release);
        pz_assert((((uintptr_t)consumer) & pz_injector_consuming_bit) == 0);
    } else {
        uintptr_t head = atomic_fetch_sub_explicit(&injector->head, pz_injector_consuming_bit, memory_order_release);
        pz_assert((head & pz_injector_consuming_bit) != 0);
    }
}

pz_nonnull(1) static pz_task* pz_injector_poll(pz_injector* injector) {
    pz_task* consumer = pz_injector_acquire(injector);
    pz_task* stolen = NULL;

    if (pz_unlikely(consumer != NULL)) {
        stolen = pz_injector_pop(injector, &consumer);
        pz_injector_release(injector, consumer);
    }

    return stolen;
}

static inline uint32_t pz_wrapping_add(uint32_t a, uint32_t b) { return a + b; }
static inline uint32_t pz_wrapping_sub(uint32_t a, uint32_t b) { return a - b; }
static inline uint32_t pz_wrapping_mul(uint32_t a, uint32_t b) { return a * b; }

pz_nonnull(1, 2, 3) static void pz_buffer_push(pz_buffer* restrict buffer, pz_task* restrict task, pz_batch* restrict batch) {
    uint32_t head = atomic_load_explicit(&buffer->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&buffer->tail, memory_order_relaxed);

    uint32_t size = pz_wrapping_sub(tail, head);
    pz_assert(size <= PZ_BUFFER_CAPACITY);

    if (size == PZ_BUFFER_CAPACITY) {
        uint32_t migrate = size / 2;
        pz_assert(migrate > 0);

        if (pz_likely(atomic_compare_exchange_strong_explicit(
            &buffer->head,
            &head,
            pz_wrapping_add(head, migrate),
            memory_order_acquire,
            memory_order_relaxed
        ))) {
            while (pz_likely(pz_checked_sub(migrate, 1, &migrate))) {
                pz_task* stolen = atomic_load_explicit(&buffer->array[head % PZ_BUFFER_CAPACITY], memory_order_relaxed);
                head = pz_wrapping_add(head, 1);
                pz_assert(stolen != NULL);
                pz_batch_push(batch, stolen);
            }
        }

        size = pz_wrapping_sub(tail, head);
        pz_assert(size <= PZ_BUFFER_CAPACITY);
    }

    pz_assert(size < PZ_BUFFER_CAPACITY);
    atomic_store_explicit(&buffer->array[tail % PZ_BUFFER_CAPACITY], task, memory_order_relaxed);
    atomic_store_explicit(&buffer->tail, pz_wrapping_add(tail, 1), memory_order_release);
}

pz_nonnull(1) static pz_task* pz_buffer_pop(pz_buffer* buffer) {
    uint32_t head = atomic_fetch_add_explicit(&buffer->head, 1, memory_order_acquire);
    uint32_t tail = atomic_load_explicit(&buffer->tail, memory_order_relaxed);

    uint32_t size = pz_wrapping_sub(tail, head);
    pz_assert(size <= PZ_BUFFER_CAPACITY);

    if (pz_unlikely(size == 0)) {
        atomic_store_explicit(&buffer->head, head, memory_order_relaxed);
        return NULL;
    }

    pz_task* stolen = atomic_load_explicit(&buffer->array[head % PZ_BUFFER_CAPACITY], memory_order_relaxed);
    pz_assert(stolen != NULL);
    return stolen;
}

pz_nonnull(1) static inline uintptr_t pz_buffer_stole(pz_buffer* buffer, uint32_t tail, uint32_t pushed) {
    pz_assert(pz_checked_sub(pushed, 1, &pushed));

    uint32_t new_tail = pz_wrapping_add(tail, pushed);
    pz_task* stolen = atomic_load_explicit(&buffer->array[new_tail % PZ_BUFFER_CAPACITY], memory_order_relaxed);
    pz_assert(stolen != NULL);

    bool submitted = new_tail != tail;
    if (submitted) {
        atomic_store_explicit(&buffer->tail, new_tail, memory_order_release);
    }

    pz_assert((((uintptr_t)stolen) & 1) == 0);
    return ((uintptr_t)stolen) | ((uintptr_t)submitted);
}

pz_nonnull(1, 2) static uintptr_t pz_buffer_steal(pz_buffer* restrict buffer, pz_buffer* restrict target) {
    while (true) {
        uint32_t target_head = atomic_load_explicit(&target->head, memory_order_acquire);
        uint32_t target_tail = atomic_load_explicit(&target->tail, memory_order_acquire);

        uint32_t target_size = pz_wrapping_sub(target_tail, target_head);
        if (pz_likely(((int32_t)target_size) <= 0)) {
            return 0;
        }

        uint32_t target_steal = target_size - (target_size / 2);
        if (pz_unlikely(target_steal > (PZ_BUFFER_CAPACITY / 2))) {
            pz_atomic_spin_loop_hint();
            continue;
        }

        uint32_t head = atomic_load_explicit(&buffer->head, memory_order_relaxed);
        uint32_t tail = atomic_load_explicit(&buffer->tail, memory_order_relaxed);
        pz_assert(tail == head);

        uint32_t pushed = 0;
        while (pz_likely(pz_checked_sub(target_steal, 1, &target_steal))) {
            uint32_t src_index = pz_wrapping_add(target_head, pushed) % PZ_BUFFER_CAPACITY;
            uint32_t dst_index = pz_wrapping_add(tail, pushed) % PZ_BUFFER_CAPACITY;
            pushed += 1;

            pz_task* stolen = atomic_load_explicit(&target->array[src_index], memory_order_relaxed);
            atomic_store_explicit(&buffer->array[dst_index], stolen, memory_order_relaxed);
            pz_assert(stolen != NULL);
        }

        if (pz_likely(atomic_compare_exchange_strong_explicit(
            &target->head,
            &target_head,
            pz_wrapping_add(target_head, pushed),
            memory_order_acq_rel,
            memory_order_relaxed
        ))) {
            return pz_buffer_stole(buffer, tail, pushed);
        }

        static _Atomic(uint32_t) contention_rng = 1;
        uint32_t rng = atomic_load_explicit(&contention_rng, memory_order_relaxed);
        uint32_t next = pz_wrapping_add(pz_wrapping_mul(rng, 1103515245UL), 12345UL);
        atomic_store_explicit(&contention_rng, next, memory_order_relaxed);

        uint32_t spin = ((next >> 24) & (128 - 1)) | (32 - 1);
        while (pz_likely(pz_checked_sub(spin, 1, &spin))) {
            pz_atomic_spin_loop_hint();
        }
    }
}

pz_nonnull(1, 2) static uintptr_t pz_buffer_inject(pz_buffer* restrict buffer, pz_injector* restrict injector) {
    pz_task* consumer = pz_injector_acquire(injector);
    if (pz_likely(consumer == NULL)) {
        return 0;
    }

    uint32_t head = atomic_load_explicit(&buffer->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&buffer->tail, memory_order_relaxed);
    pz_assert(tail == head);

    uint32_t pushed = 0;
    while (pz_likely(pushed < PZ_BUFFER_CAPACITY)) {
        pz_task* stolen = pz_injector_pop(injector, &consumer);
        if (pz_unlikely(stolen == NULL)) {
            break;
        }

        uint32_t index = pz_wrapping_add(tail, pushed) % PZ_BUFFER_CAPACITY;
        atomic_store_explicit(&buffer->array[index], stolen, memory_order_relaxed);
        pushed += 1;
    }

    pz_injector_release(injector, consumer);
    return pz_buffer_stole(buffer, tail, pushed);
}
