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
    pz_runnable* front = batch->head;
    if (PZ_UNLIKELY(front == NULL)) return;

    pz_runnable* back = batch->tail;
    PZ_ASSERT(back != NULL);

    pz_runnable* tail = atomic_exchange_explicit(&injector->tail, back, memory_order_release);
    if (PZ_LIKELY(tail != NULL)) {
        atomic_store_explicit(&tail->next, front, memory_order_release);
        return;
    }

    uintptr_t head = atomic_fetch_add_explicit(&injector->head, (uintptr_t)front, memory_order_release);
    PZ_ASSERT((head & ~PZ_INJECTOR_CONSUMING) == 0);
}

PZ_NONNULL(1)
static pz_runnable* pz_injector_acquire(pz_injector* restrict injector) {
    uintptr_t head = atomic_load_explicit(&injector->head, memory_order_relaxed);
    while (true) {
        // Check if there's a pending list of runnables to consume. 
        pz_runnable* consumer = (pz_runnable*)(head & ~PZ_INJECTOR_CONSUMING);
        if (PZ_LIKELY(consumer == NULL)) {
            return NULL;
        }

        // Check if the injector's consuming end is already acquired.
        if ((head & PZ_INJECTOR_CONSUMING) != 0) {
            return NULL;
        }

        if (PZ_LIKELY(atomic_compare_exchange_weak_explicit(
            &injector->head,
            &head,
            PZ_INJECTOR_CONSUMING,
            memory_order_acquire,
            memory_order_relaxed
        ))) return consumer;
    }
}

PZ_NONNULL(1, 2)
static pz_runnable* pz_injector_pop(pz_injector* restrict injector, pz_runnable** restrict consumer) {
    pz_runnable* front = *consumer;
    if (PZ_UNLIKELY(front == NULL)) {
        uintptr_t head = atomic_load_explicit(&injector->head, memory_order_acquire);
        PZ_ASSERT((head & PZ_INJECTOR_CONSUMING) != 0);

        front = (pz_runnable*)(head & ~PZ_INJECTOR_CONSUMING);
        if (PZ_UNLIKELY(front == NULL)) {
            return NULL;
        }

        *consumer = front;
        atomic_store_explicit(&injector->head, PZ_INJECTOR_CONSUMING, memory_order_relaxed);
    }

    pz_runnable* next = atomic_load_explicit(&front->next, memory_order_acquire);
    if (PZ_UNLIKELY(next == NULL)) {
        // This looks like it's the last runnable. Try to take it from the producers.
        pz_runnable* tail = front;
        if (PZ_UNLIKELY(atomic_compare_exchange_strong_explicit(
            &injector->tail,
            &tail,
            NULL,
            memory_order_acq_rel,
            memory_order_acquire
        ))) {
            // Failure to grab the tail means a producer came in and swapped in the meantime.
            // See if it also gets to setting the last runnable's next link.
            // If not, we falsely report being empty but the caller should handle that.
            pz_atomic_spin_loop_hint();
            next = atomic_load_explicit(&front->next, memory_order_acquire);
            if (PZ_UNLIKELY(next == NULL)) {
                return NULL;
            }
        }
    }

    *consumer = next;
    return front;
}

PZ_NONNULL(1)
static void pz_injector_release(pz_injector* restrict injector, const pz_runnable* consumer) {
    if (PZ_UNLIKELY(consumer != NULL)) {
        atomic_store_explicit(&injector->head, (uintptr_t)consumer, memory_order_release);
        return;
    }

    uintptr_t head = atomic_fetch_sub_explicit(&injector->head, PZ_INJECTOR_CONSUMING, memory_order_release);
    PZ_ASSERT((head & PZ_INJECTOR_CONSUMING) != 0);
}

PZ_NONNULL(1, 2, 3)
static void pz_buffer_push(pz_buffer* restrict buffer, pz_runnable* restrict runnable, pz_batch* restrict batch) {
    uint32_t head = atomic_load_explicit(&buffer->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&buffer->tail, memory_order_relaxed);

    uint32_t size = PZ_WRAPPING_SUB(tail, head);
    PZ_ASSERT(size <= PZ_BUFFER_CAPACITY);

    if (size == PZ_BUFFER_CAPACITY) {
        uint32_t migrate = size / 2;
        PZ_ASSERT(migrate > 0);

        if (PZ_LIKELY(atomic_compare_exchange_strong_explicit(
            &buffer->head,
            &head,
            PZ_WRAPPING_ADD(head, migrate),
            memory_order_acquire,
            memory_order_relaxed
        ))) {
            while (PZ_LIKELY(PZ_OVERFLOW_SUB(migrate, 1, &migrate))) {
                pz_runnable* stolen = atomic_load_explicit(&buffer->array[head % PZ_BUFFER_CAPACITY], memory_order_relaxed);
                PZ_ASSERT(stolen != NULL);
                pz_batch_push(batch, stolen);
                head = PZ_WRAPPING_ADD(head, 1);
            }
        }

        size = PZ_WRAPPING_SUB(tail, head);
        PZ_ASSERT(size <= PZ_BUFFER_CAPACITY);
    }

    PZ_ASSERT(size < PZ_BUFFER_CAPACITY);
    atomic_store_explicit(&buffer->array[tail % PZ_BUFFER_CAPACITY], runnable, memory_order_relaxed);
    atomic_store_explicit(&buffer->tail, PZ_WRAPPING_ADD(tail, 1), memory_order_release);
}

PZ_NONNULL(1)
static pz_runnable* pz_buffer_pop(pz_buffer* buffer) {
    uint32_t head = atomic_fetch_add_explicit(&buffer->head, 1, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&buffer->tail, memory_order_relaxed);

    uint32_t size = PZ_WRAPPING_SUB(tail, head);
    PZ_ASSERT(size <= PZ_BUFFER_CAPACITY);

    if (PZ_UNLIKELY(size == 0)) {
        atomic_store_explicit(&buffer->head, head, memory_order_relaxed);
        return NULL;
    }

    pz_runnable* runnable = atomic_load_explicit(&buffer->array[head % PZ_BUFFER_CAPACITY], memory_order_relaxed);
    PZ_ASSERT(runnable != NULL);
    return runnable;
}

PZ_NONNULL(1)
static uintptr_t pz_buffer_commit(pz_buffer* buffer, uint32_t tail, uint32_t pushed) {
    PZ_ASSERT(PZ_OVERFLOW_SUB(pushed, 1, &pushed));

    uint32_t new_tail = PZ_WRAPPING_ADD(tail, pushed);
    pz_runnable* runnable = atomic_load_explicit(&buffer->array[new_tail % PZ_BUFFER_CAPACITY], memory_order_relaxed);
    PZ_ASSERT(runnable != NULL);

    bool submitted = new_tail != tail;
    if (submitted) {
        atomic_store_explicit(&buffer->tail, new_tail, memory_order_release);
    }

    PZ_ASSERT((((uintptr_t)runnable) & 1) == 0);
    return ((uintptr_t)runnable) | ((uintptr_t)submitted);
}

PZ_NONNULL(1, 2)
static uintptr_t pz_buffer_steal(pz_buffer* restrict buffer, pz_buffer* restrict target) {
    while (true) {
        uint32_t target_head = atomic_load_explicit(&target->head, memory_order_acquire);
        uint32_t target_tail = atomic_load_explicit(&target->tail, memory_order_acquire);

        uint32_t target_size = PZ_WRAPPING_SUB(target_tail, target_head);
        if (PZ_UNLIKELY(((int32_t)target_size) <= 0)) {
            return 0;
        }

        uint32_t target_steal = target_size - (target_size / 2);
        if (PZ_UNLIKELY(target_steal > (PZ_BUFFER_CAPACITY / 2))) {
            pz_atomic_spin_loop_hint();
            continue;
        }

        uint32_t head = atomic_load_explicit(&buffer->head, memory_order_relaxed);
        uint32_t tail = atomic_load_explicit(&buffer->tail, memory_order_relaxed);
        PZ_ASSERT(head == tail);

        uint32_t pushed = 0;
        while (PZ_LIKELY(pushed < PZ_BUFFER_CAPACITY)) {
            uint32_t src_index = PZ_WRAPPING_ADD(target_head, pushed) % PZ_BUFFER_CAPACITY;
            uint32_t dst_index = PZ_WRAPPING_ADD(tail, pushed) % PZ_BUFFER_CAPACITY;
            pushed += 1;

            pz_runnable* stolen = atomic_load_explicit(&target->array[src_index], memory_order_relaxed);
            atomic_store_explicit(&buffer->array[dst_index], stolen, memory_order_relaxed);
            PZ_ASSERT(stolen != NULL);
        }

        if (PZ_UNLIKELY(atomic_compare_exchange_strong_explicit(
            &target->head,
            &target_head,
            PZ_WRAPPING_ADD(target_head, pushed),
            memory_order_acq_rel,
            memory_order_relaxed
        ))) {
            return pz_buffer_commit(buffer, tail, pushed);
        }

        static _Atomic(uint32_t) prng = 1;
        uint32_t rng = atomic_load_explicit(&prng, memory_order_relaxed);
        uint32_t next = PZ_WRAPPING_ADD(PZ_WRAPPING_MUL(rng, 1103515245UL), 12345);
        atomic_store_explicit(&prng, next, memory_order_relaxed);

        uint32_t spin = ((next >> 24) & (128 - 1)) | (32 - 1);
        while (PZ_LIKELY(PZ_OVERFLOW_SUB(spin, 1, &spin))) {
            pz_atomic_spin_loop_hint();
        }
    }
}

PZ_NONNULL(1, 2)
static uintptr_t pz_buffer_inject(pz_buffer* restrict buffer, pz_injector* restrict injector) {
    pz_runnable* consumer = pz_injector_acquire(injector);
    if (PZ_UNLIKELY(consumer == NULL)) {
        return 0;
    }

    uint32_t head = atomic_load_explicit(&buffer->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&buffer->tail, memory_order_relaxed);
    PZ_ASSERT(head == tail);

    uint32_t pushed = 0;
    while (PZ_LIKELY(pushed < PZ_BUFFER_CAPACITY)) {
        pz_runnable* stolen = pz_injector_pop(injector, &consumer);
        if (PZ_UNLIKELY(stolen == NULL)) {
            break;
        }

        uint32_t index = PZ_WRAPPING_ADD(tail, pushed) % PZ_BUFFER_CAPACITY;
        atomic_store_explicit(&buffer->array[index], stolen, memory_order_relaxed);
        pushed += 1;
    }

    pz_injector_release(injector, consumer);
    return pz_buffer_commit(buffer, tail, pushed);
}
