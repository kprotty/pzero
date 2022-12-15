#include "queue.h"

static void pz_queue_list_push(struct pz_queue_list* NOALIAS list, struct pz_queue_node* NOALIAS node) {
    ASSUME(list != NULL);
    ASSUME(node != NULL);

    // Swap the tail with the new node, marking it as the new tail.
    struct pz_queue_node* tail = list->tail;
    list->tail = node;
    pz_queue_node_set_next(node, NULL);

    // Link the previous tail (or the head if no previous) to the new node.
    if (LIKELY(tail != NULL)) {
        pz_queue_node_set_next(tail, node);
    } else {
        list->head = node;
    }
}

static bool pz_queue_list_push_all(struct pz_queue_list* NOALIAS list, const struct pz_queue_list* NOALIAS target) {
    ASSUME(list != NULL);
    ASSUME(target != NULL);

    struct pz_queue_node* target_head = target->head;
    if (UNLIKELY(target_head == NULL)) {
        return false;
    }

    struct pz_queue_node* target_tail = target->tail;
    ASSERT(target_tail != NULL, "invalid target list with head but no tail");
    ASSERT(pz_queue_node_get_next(target_tail) == NULL, "invalid target list tail but next != NULL");

    struct pz_queue_node* head = list->head;
    if (UNLIKELY(head == NULL)) {
        list->head = target_head;
        list->tail = target_tail;
        return true;
    }

    struct pz_queue_node* tail = list->tail;
    ASSERT(tail != NULL, "invalid list with head but no tail");
    ASSERT(pz_queue_node_get_next(tail) == NULL, "invalid list with tail but next != NULL");

    pz_queue_node_set_next(tail, target_head);
    list->tail = target_tail;
    return true;
}

static struct pz_queue_node* pz_queue_list_pop(struct pz_queue_list* list) {
    ASSUME(list != NULL);

    struct pz_queue_node* node = list->head;
    if (LIKELY(node != NULL)) {
        // Advance the head.
        struct pz_queue_node* next = pz_queue_node_get_next(node);
        list->head = next;

        // Zero out the tail once we reach the end.
        ASSERT(list->tail != NULL, "invalid list with head but no tail");
        if (next == NULL) {
            list->tail = NULL;
        }
    }

    return node;
}

static const uintptr_t INJECT_CONSUMING_BIT = UPTR(1);
static const uintptr_t INJECT_NODE_MASK = ~INJECT_CONSUMING_BIT;

static void pz_queue_injector_push(struct pz_queue_injector* NOALIAS injector, struct pz_queue_list* NOALIAS list) {
    ASSUME(injector != NULL);
    ASSUME(list != NULL);

    // Check if the list to push is empty.
    struct pz_queue_node* front = list->head;
    if (front == NULL) {
        return;
    }

    // Make sure there's an end to the list.
    struct pz_queue_node* back = list->tail;
    ASSERT(back != NULL, "pushing invalid list with head but no tail");
    ASSERT(pz_queue_node_get_next(back) == NULL, "pushing invalid list with tail->next != NULL");

    // Update the end of the injector to the end of the list.
    // Release ordering ensures the list's node links being formed prior happens-before the injector's list is updated.
    uintptr_t tail = atomic_swap_uptr_release(&injector->tail, UPTR(back));

    // If the injector had a previous list, link it to the current one.
    // Release ordering ensures that the injector list being updated happens-before we make our list available to the previous one (consumer).
    struct pz_queue_node* prev = (struct pz_queue_node*)tail;
    if (LIKELY(prev != NULL)) {
        atomic_store_uptr_release(&prev->next, UPTR(front));
        return;
    }

    // An injector without a previous list means it's the first to enqueue on empty so the head should be set for the consumer.
    // The consumer may still hold/update the CONSUMING bit, so we update the head pointer using an RMW instead of a STORE.
    // Release ordering ensures that the injector list being updated happens-before we announce our list as available to the head (consumer).
    ASSUME((UPTR(front) & INJECT_CONSUMING_BIT) == 0);
    uintptr_t head = atomic_fetch_add_uptr_release(&injector->head, UPTR(front));
    ASSERT((head & INJECT_NODE_MASK) == 0, "injector pushed to head when it was not empty");
}

static pz_queue_consumer pz_queue_consumer_acquire(struct pz_queue_injector* NOALIAS injector) {
    ASSUME(injector != NULL);

    uintptr_t head = atomic_load_uptr(&injector->head);
    while (true) {
        // Bail if there's no head node to start popping with.
        // pz_queue_injector_push() will set the head node on first enqueue.
        // We assume this is likely to be empty as injectors are checked frequently but rarely available.
        // When they are available, the caller attempts to consume all of their nodes so they're likely to be empty next time.
        struct pz_queue_node* head_node = (struct pz_queue_node*)(head & INJECT_NODE_MASK);
        if (LIKELY(head_node == NULL)) {
            return NULL;
        }

        // Can't pop nodes if there's currently an active consumer.
        // This can happen if the active consumer has drained all nodes but a producer has added more.
        if (UNLIKELY((head & INJECT_CONSUMING_BIT) != 0)) {
            return NULL;
        }

        // Try to acquire the current head of the injected list and mark is as actively consuming.
        // The `consumer` will hold the node head pointer until it releases the CONSUMING bit.
        // If the consumer empties the injected list, the producer will set injector->head without disrupting the CONSUMING bit.
        // Acquire barrier ensures link updates by previous consumers happen-before our thread starts consuming.
        if (LIKELY(atomic_cas_uptr_acquire(&injector->head, &head, UPTR(INJECT_CONSUMING_BIT)))) {
            return head_node;
        }
    }
}

static struct pz_queue_node* pz_queue_consumer_pop(pz_queue_consumer* NOALIAS consumer, struct pz_queue_injector* NOALIAS injector) {
    ASSUME(consumer != NULL);
    ASSUME(injector != NULL);

    // Try to dequeue the head of the injector list we are consuming.
    struct pz_queue_node* node = *consumer;
    if (UNLIKELY(node == NULL)) {
        // No head means a producer thread will set it for us on injector->head without disturbing the CONSUMING bit.
        // Acquire barrier ensures the producer's forming of its injected list happens-before we start consuming it.
        uintptr_t head = atomic_load_uptr_acquire(&injector->head);
        ASSERT((head & INJECT_CONSUMING_BIT) != 0, "consumer popped from injector without the CONSUMING bit");

        node = (struct pz_queue_node*)(head & INJECT_NODE_MASK);
        if (UNLIKELY(node == NULL)) {
            return NULL;
        }
        
        // At this point, the producer can no longer update injector->head as the queue is non-empty.
        // This means we can use a STORE instead of an RMW to reset injector->head back to the consuming state.
        *consumer = node;
        atomic_store_uptr(&injector->head, UPTR(INJECT_CONSUMING_BIT));
    }

    // Try to dequeue the `node` head by replacing it with the next node in the injected list.
    // Acquire barrier ensures the producer linking the injected list together happens-before we dequeue the node.
    struct pz_queue_node* next = (struct pz_queue_node*)atomic_load_uptr_acquire(&node->next);
    if (UNLIKELY(next == NULL)) {
        // A node without a next means it should be the last node in the injected list.
        // To dequeue it, we must also zero out injector->tail in case another producer tries to link to it later.
        //
        // Release barrier ensures injector->head's NODE_MASK being zeroed out happens-before we try to zero-out the tail,
        // as a producer who sees an empty tail will link its injected list to injector->head's NODE_MASK.
        //
        // Acquire barrier ensures that the attempt to zero out injector->tail happens-before the
        // re-check on the head's next node on failure. 
        uintptr_t current_tail = UPTR(node);
        if (UNLIKELY(!atomic_cas_uptr_acq_rel(&injector->tail, &current_tail, UPTR(0)))) {
            // Failing to zero out the tail means that a new list was injected after our node->next check.
            // Check one last time in the hopes that said producer thread is still running and linked its injected list to the current head node.
            next = (struct pz_queue_node*)atomic_load_uptr_acquire(&node->next);
            if (UNLIKELY(next == NULL)) {
                return NULL;
            }
        }
    }

    *consumer = next;
    return node;
}

static void pz_queue_consumer_release(pz_queue_consumer NOALIAS consumer, struct pz_queue_injector* NOALIAS injector) {
    ASSUME(injector != NULL);

    // Unset the CONSUMING bit while setting the updated head after popping some nodes.
    // If there's still nodes left, we're guaranteed that the producer wont update injected->head so we can use STORE instead of an RMW.
    // This is unlikely to happen as a caller who acquires a consumer generally tries to drain it of all nodes.
    // Release barier ensures that all pz_queue_consumer_pop()'s prior happen-before we release the CONSUMING bit.
    if (UNLIKELY(consumer != NULL)) {
        atomic_store_uptr_release(&injector->head, UPTR(consumer));
        return;
    }

    // If there's no nodes left, then a concurrent producer thread may see an empty list and update the injector->head's NODE_MASK bits so we use RMW instead of STORE.
    // Release barier ensures that all pz_queue_consumer_pop()'s prior happen-before we release the CONSUMING bit.
    uintptr_t head = atomic_fetch_sub_uptr_release(&injector->head, INJECT_CONSUMING_BIT);
    ASSERT((head & INJECT_CONSUMING_BIT) != 0, "consumer was popping without having set the CONSUMING bit");
}

static void pz_queue_buffer_push(struct pz_queue_buffer* NOALIAS buffer, struct pz_queue_node* NOALIAS node, struct pz_queue_list* NOALIAS overflowed) {
    ASSUME(buffer != NULL);
    ASSUME(node != NULL);
    ASSUME(overflowed != NULL);
    
    // The head must be loaded atomically as other threads in pz_queue_buffer_steal() may be updating it.
    // The tail doesn't need to be loaded atomically, but non-atomic loads of an atomic_uptr's modification order are undefined..
    uint16_t head = (uint16_t)atomic_load_uptr(&buffer->head);
    uint16_t tail = (uint16_t)atomic_load_uptr(&buffer->tail);

    uint16_t size = WRAPPING_SUB(tail, head);
    ASSERT(size <= PZ_BUFFER_CAPACITY, "invalid buffer size when pushing");

    // If the buffer is full, try to migrate half of the nodes into the overflowed list.
    // This is unlikely due to being an amortized event.
    if (UNLIKELY(size == PZ_BUFFER_CAPACITY)) {
        uint16_t new_head = WRAPPING_ADD(head, size / 2);
        ASSERT(new_head != head, "uint16_t cannot hold PZ_BUFFER_CAPACITY or it's zero");

        // Try to bump the head forward to steal half the nodes for migration.
        // This competes with pz_queue_buffer_steal() threads and failure implies nodes were stolen by others so there's room to push.
        // Acquire barrier ensures that a successful steal happens-before the list_push() node writes from below. 
        uintptr_t current_head = UPTR(head);
        if (LIKELY(atomic_cas_uptr_acquire(&buffer->head, &current_head, UPTR(new_head)))) {
            while (LIKELY(head != new_head)) {
                // Reading the node to migrate from the buffer doesn't have to be atomic here,
                // but non-atomic reads from an atomic_uptr's modification order are undefined..
                uintptr_t stolen = atomic_load_uptr(&buffer->array[head % PZ_BUFFER_CAPACITY]);
                head = WRAPPING_ADD(head, 1);

                struct pz_queue_node* migrated = (struct pz_queue_node*)stolen;
                ASSERT(migrated != NULL, "buffer_push() migrated an invalid node");
                pz_queue_list_push(overflowed, migrated);
            }

            // Append the node we were supposed to push also to the stolen nodes.
            pz_queue_list_push(overflowed, node);
            return;
        }

        // Failure to migrate means pz_queue_buffer_steal() threads made room.
        size = WRAPPING_SUB(tail, (uint16_t)current_head);
        ASSERT(size <= PZ_BUFFER_CAPACITY, "invalid buffer size when failing to migrate");
    }

    // Write the node to the buffer's array and bump the tail.
    // The write to the array must be atomic as other thread sin pz_queue_buffer_steal() may be reading the slots with their CAS about to fail.
    // Release barrier ensures that the node being prepared happens-before the tail is updated to publish it for stealing.
    ASSERT(size < PZ_BUFFER_CAPACITY, "invalid buffer size when trying to push to array");
    atomic_store_uptr(&buffer->array[tail % PZ_BUFFER_CAPACITY], UPTR(node));
    atomic_store_uptr_release(&buffer->tail, WRAPPING_ADD(tail, 1));
}

static uintptr_t pz_queue_buffer_pop(struct pz_queue_buffer* buffer) {
    ASSUME(buffer != NULL);

    uint16_t head = (uint16_t)atomic_fetch_add_uptr_acquire(&buffer->head, 1);
    uint16_t tail = (uint16_t)atomic_load_uptr(&buffer->tail);

    uint16_t size = WRAPPING_SUB(tail, head);
    ASSERT(size <= PZ_BUFFER_CAPACITY, "invalid buffer size when popping");

    if (UNLIKELY(size == 0)) {
        atomic_store_uptr(&buffer->head, UPTR(head));
        return 0;
    }

    // Read the stolen node from the buffer's array.
    // This doesn't have to be atomic, but a non-atomic read from an atomic_uptr's modification order is undefined..
    uintptr_t stolen = atomic_load_uptr(&buffer->array[head % PZ_BUFFER_CAPACITY]);
    struct pz_queue_node* node = (struct pz_queue_node*)stolen;
    ASSERT(node != NULL, "returning an invalid stolen node");
    return UPTR(node);
}

static inline uintptr_t pz_queue_buffer_report_stolen(struct pz_queue_buffer* buffer, pz_queue_producer* NOALIAS producer) {
    ASSUME(buffer != NULL);
    ASSUME(producer != NULL);

    struct pz_queue_node* node = pz_queue_producer_pop(producer, buffer);
    if (UNLIKELY(node == NULL)) {
        return 0;
    }

    bool pushed = pz_queue_producer_commit(*producer, buffer);
    ASSERT((UPTR(node) & 1) == 0, "returning a misaligned stolen node");
    return UPTR(node) | UPTR(pushed);
}

static uintptr_t pz_queue_buffer_steal(struct pz_queue_buffer* NOALIAS buffer, struct pz_queue_buffer* NOALIAS target, struct pz_random* NOALIAS rng) {
    ASSUME(buffer != NULL);
    ASSUME(target != NULL);
    ASSUME(buffer != target);
    ASSUME(rng != NULL);

    while (true) {
        uint16_t target_head = (uint16_t)atomic_load_uptr_acquire(&target->head);
        uint16_t target_tail = (uint16_t)atomic_load_uptr_acquire(&target->tail);

        uint16_t target_size = WRAPPING_SUB(target_tail, target_head);
        if (UNLIKELY(((int16_t)target_size) <= 0)) {
            return 0;
        }

        uint16_t target_steal = target_size - (target_size / 2);
        if (UNLIKELY(target_steal > (PZ_BUFFER_CAPACITY / 2))) {
            continue;
        }

        uint16_t new_target_head = target_head;
        pz_queue_producer producer = pz_queue_producer_init(buffer);

        while (LIKELY(CHECKED_SUB(target_steal, 1, &target_steal))) {
            uintptr_t stolen = atomic_load_uptr(&target->array[new_target_head % PZ_BUFFER_CAPACITY]);
            new_target_head = WRAPPING_ADD(new_target_head, 1);

            struct pz_queue_node* node = (struct pz_queue_node*)stolen;
            ASSERT(node != NULL, "buffer is stealing an invalid node from target");

            bool pushed = pz_queue_producer_push(&producer, buffer, node);
            ASSERT(pushed, "buffer is stealing when not empty");
        }

        uintptr_t current_target_head = UPTR(target_head);
        if (UNLIKELY(!atomic_cas_uptr_acq_rel(&target->head, &current_target_head, UPTR(new_target_head)))) {
            atomic_hint_backoff(rng);
            continue;
        }

        return pz_queue_buffer_report_stolen(buffer, &producer);
    }
}

static uintptr_t pz_queue_buffer_inject(struct pz_queue_buffer* NOALIAS buffer, struct pz_queue_injector* NOALIAS injector) {
    ASSUME(buffer != NULL);
    ASSUME(injector != NULL);

    pz_queue_consumer consumer = pz_queue_consumer_acquire(injector);
    if (LIKELY(consumer == NULL)) {
        return 0; 
    }

    pz_queue_producer producer = pz_queue_producer_init(buffer);
    uint16_t available = PZ_BUFFER_CAPACITY;

    while (LIKELY(CHECKED_SUB(available, 1, &available))) {
        struct pz_queue_node* node = pz_queue_consumer_pop(&consumer, injector);
        if (UNLIKELY(node == NULL)) {
            break;
        }

        bool pushed = pz_queue_producer_push(&producer, buffer, node);
        ASSERT(pushed, "buffer is injecting when not empty");
    }

    pz_queue_consumer_release(consumer, injector);
    return pz_queue_buffer_report_stolen(buffer, &producer);
}

static uintptr_t pz_queue_buffer_fill(struct pz_queue_buffer* NOALIAS buffer, struct pz_queue_list* NOALIAS list) {
    ASSUME(buffer != NULL);
    ASSUME(list != NULL);

    pz_queue_producer producer = pz_queue_producer_init(buffer);
    uint16_t available = PZ_BUFFER_CAPACITY;

    while (LIKELY(CHECKED_SUB(available, 1, &available))) {
        struct pz_queue_node* node = pz_queue_list_pop(list);
        if (UNLIKELY(node == NULL)) {
            break;
        }

        bool pushed = pz_queue_producer_push(&producer, buffer, node);
        ASSERT(pushed, "buffer is filling when not empty");
    }

    return pz_queue_buffer_report_stolen(buffer, &producer);
}

enum {
    HEAD_SHIFT = 0,
    TAIL_SHIFT = 16,
    PUSHED_SHIFT = 32,
    AVAILABLE_SHIFT = 48,
};

static pz_queue_producer pz_queue_producer_init(struct pz_queue_buffer* NOALIAS buffer) {
    uint16_t head = (uint16_t)atomic_load_uptr(&buffer->head);
    uint16_t tail = (uint16_t)(*((uintptr_t*)&buffer->tail));

    uint16_t size = WRAPPING_SUB(tail, head);
    ASSERT(size <= PZ_BUFFER_CAPACITY, "invalid buffer size when producing");

    uint64_t position = 0;
    position |= ((uint64_t)head) << HEAD_SHIFT;
    position |= ((uint64_t)tail) << TAIL_SHIFT;
    position |= ((uint64_t)0) << PUSHED_SHIFT;
    position |= ((uint64_t)(PZ_BUFFER_CAPACITY - size)) << AVAILABLE_SHIFT;
    return position;
}

static bool pz_queue_producer_push(pz_queue_producer* NOALIAS producer, struct pz_queue_buffer* NOALIAS buffer, struct pz_queue_node* NOALIAS node) {
    ASSUME(producer != NULL);
    ASSUME(buffer != NULL);
    ASSUME(node != NULL);
    
    uint64_t position = *producer;
    uint16_t tail = (uint16_t)(position >> TAIL_SHIFT);
    uint16_t pushed = (uint16_t)(position >> PUSHED_SHIFT);
    uint16_t available = (uint16_t)(position >> AVAILABLE_SHIFT);

    if (UNLIKELY(available == 0)) {
        return false;
    }

    position = WRAPPING_ADD(position, 1ULL << PUSHED_SHIFT);
    position = WRAPPING_SUB(position, 1ULL << AVAILABLE_SHIFT);
    *producer = position;

    // Slot write must be atomic to avoid data-race from other threads concurrently reading who are about to fail their CAS.
    atomic_uptr* slot = &buffer->array[WRAPPING_ADD(tail, pushed) % PZ_BUFFER_CAPACITY];
    atomic_store_uptr(slot, UPTR(node));
    return true;
}

static struct pz_queue_node* pz_queue_producer_pop(pz_queue_producer* NOALIAS producer, struct pz_queue_buffer* NOALIAS buffer) {
    ASSUME(producer != NULL);
    ASSUME(buffer != NULL);
    
    uint64_t position = *producer;
    uint16_t tail = (uint16_t)(position >> TAIL_SHIFT);
    uint16_t pushed = (uint16_t)(position >> PUSHED_SHIFT);
    
    if (UNLIKELY(pushed == 0)) {
        return NULL;
    }

    position = WRAPPING_SUB(position, 1ULL << PUSHED_SHIFT);
    position = WRAPPING_ADD(position, 1ULL << AVAILABLE_SHIFT);
    *producer = position;

    // Read doesn't have to be atomic as we're the only writer, but non-atomic reads from atomic_uptr's modification order are undefined..
    atomic_uptr* slot = &buffer->array[WRAPPING_ADD(tail, pushed) % PZ_BUFFER_CAPACITY];
    struct pz_queue_node* node = (struct pz_queue_node*)atomic_load_uptr(slot); 
    ASSERT(node != NULL, "producer popped a node that was never pushed");
    return node;
}

static bool pz_queue_producer_commit(pz_queue_producer producer, struct pz_queue_buffer* NOALIAS buffer) {
    ASSUME(producer != 0);
    ASSUME(buffer != NULL);
    
    uint64_t position = producer;
    uint16_t tail = (uint16_t)(position >> TAIL_SHIFT);
    uint16_t pushed = (uint16_t)(position >> PUSHED_SHIFT);

    bool committed = pushed > 0;
    if (LIKELY(committed)) {
        uint16_t new_tail = WRAPPING_ADD(tail, pushed);
        atomic_store_uptr_release(&buffer->tail, new_tail);
    }
    
    return committed;
}

