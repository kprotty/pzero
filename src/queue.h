#ifndef _PZ_QUEUE_H
#define _PZ_QUEUE_H

#include "atomic.h"
#include "random.h"

struct pz_queue_node {
    atomic_uptr next;
};

static inline void pz_queue_node_set_next(struct pz_queue_node* node, struct pz_queue_node* next) {
    // NOTE: doesn't have to be atomic, but non-atomic writes to atomic_uptr's modification order are undefined..
    atomic_store_uptr(&node->next, UPTR(next));
}

static inline struct pz_queue_node* pz_queue_node_get_next(struct pz_queue_node* node) {
    // NOTE: doesn't have to be atomic, but non-atomic reads from atomic_uptr's modification order are undefined..
    return (struct pz_queue_node*)atomic_load_uptr(&node->next);
}

struct pz_queue_list {
    struct pz_queue_node* head;
    struct pz_queue_node* tail;
};

static void pz_queue_list_push(struct pz_queue_list* NOALIAS list, struct pz_queue_node* NOALIAS node);

static bool pz_queue_list_push_all(struct pz_queue_list* NOALIAS list, const struct pz_queue_list* NOALIAS target);

static struct pz_queue_node* pz_queue_list_pop(struct pz_queue_list* list);

struct pz_queue_injector {
    /// The head node pointer has its own cache line to prevent producers from experiencing
    /// false sharing done by possibly multiple consumers trying to update the head.
    atomic_uptr head;
    uint8_t _head_cache_padding[ATOMIC_CACHE_LINE_ASSUMED - sizeof(atomic_uptr)];

    /// The tail node pointer has its own cache line to prevent the consumer from experiencing
    // false-sharing done by multiple producers updating the tail.
    atomic_uptr tail;
    uint8_t _tail_cache_padding[ATOMIC_CACHE_LINE_ASSUMED - sizeof(atomic_uptr)];
};

static void pz_queue_injector_push(struct pz_queue_injector* NOALIAS injector, struct pz_queue_list* NOALIAS list);

typedef struct pz_queue_node* pz_queue_consumer;

/// Tries to acquire a consumer on the MPSC injector.
/// Returns true if the caller is now the injector's consumer.
static pz_queue_consumer pz_queue_consumer_acquire(struct pz_queue_injector* NOALIAS injector);

/// Dequeue a node that was pushed to the injector.
/// It's assumed that the caller has acquired the consumer on the same injector.
static struct pz_queue_node* pz_queue_consumer_pop(pz_queue_consumer* NOALIAS consumer, struct pz_queue_injector* NOALIAS injector);

/// Release the consumer, allowing other threads to acquire one on the injector to dequeue nodes.
/// It's assumed that the caller has acquired the consumer on the same injector.
static void pz_queue_consumer_release(pz_queue_consumer NOALIAS consumer, struct pz_queue_injector* NOALIAS injector);

enum {
    /// After previous benchmarking, this is good limit for the buffer size.
    /// Throughput gains plateaus heavily after 128-256, a power of two allows for fast index reducing, 
    /// and it's the same worker-local buffer size used in Golang and Rust Tokio.
    PZ_BUFFER_CAPACITY = 256,
};

struct pz_queue_buffer {
    /// The head (index) is on its own cache line to avoid the buffer's single producer from 
    /// experiencing false-sharing when there's concurrent consumers
    atomic_uptr head;
    uint8_t _cache_padding[ATOMIC_CACHE_LINE_ASSUMED - sizeof(atomic_uptr)];

    /// The tail (index) doesn't really need to be on a different cache-line than the array (of nodes)
    /// as the buffer's single producer will write to both the array and the tail when enqueueing.
    atomic_uptr tail;
    atomic_uptr array[PZ_BUFFER_CAPACITY];
};

static void pz_queue_buffer_push(struct pz_queue_buffer* NOALIAS buffer, struct pz_queue_node* NOALIAS node, struct pz_queue_list* NOALIAS overflowed);

static uintptr_t pz_queue_buffer_pop(struct pz_queue_buffer* NOALIAS buffer);

static uintptr_t pz_queue_buffer_steal(struct pz_queue_buffer* NOALIAS buffer, struct pz_queue_buffer* NOALIAS target, struct pz_random* NOALIAS rng);

static uintptr_t pz_queue_buffer_inject(struct pz_queue_buffer* NOALIAS buffer, struct pz_queue_injector* NOALIAS injector);

static uintptr_t pz_queue_buffer_fill(struct pz_queue_buffer* NOALIAS buffer, struct pz_queue_list* NOALIAS list);

typedef uint64_t pz_queue_producer;

static pz_queue_producer pz_queue_producer_init(struct pz_queue_buffer* NOALIAS buffer);

static bool pz_queue_producer_push(pz_queue_producer* NOALIAS producer, struct pz_queue_buffer* NOALIAS buffer, struct pz_queue_node* NOALIAS node);

static struct pz_queue_node* pz_queue_producer_pop(pz_queue_producer* NOALIAS producer, struct pz_queue_buffer* NOALIAS buffer);

static bool pz_queue_producer_commit(pz_queue_producer producer, struct pz_queue_buffer* NOALIAS buffer);



#endif // _PZ_QUEUE_H
