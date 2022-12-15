#ifndef _PZ_WORKER_H
#define _PZ_WORKER_H

#include "builtin.h"
#include "queue.h"

struct pz_worker {
    pz_queue_buffer runnable;
    pz_queue_injector overflowed;

    pz_queue_injector injected;

    pz_queue_list local;
};

struct pz_context = *worker;

void pz_context_submit(struct pz_context* NOALIAS context, int worker_id, struct pz_task* NOALIAS task);

struct pz_batch {
    pz_queue_list list;
    pz_worker* worker;
    pz_queue_buffer_producer producer;
    bool (*commit_callback)(struct pz_batch* batch);
};

int pz_batch_init(struct pz_batch* NOALIAS batch, struct pz_context* NOALIAS context, int worker_id) {
    batch->list = { null, null };
    batch->worker = context;
    batch->producer = 0;
    
    if (LIKELY(worker_id == ANY_WORKER)) {
        pz_queue_buffer_producer_init(&batch->producer, &worker->runnable);
        batch->commit_callback = pz_batch_shared_commit;
        return 0;
    }

    if (worker_id == context->worker_id) {
        batch->commit_callback = pz_batch_local_commit;
        return 0;
    }

    struct pz_scheduler* scheduler = context->scheduler;
    if (worker_id >= scheduler->max_workers) {
        return -1;
    }

    remote_worker = atomic_load_uptr_acquire(&scheduler->workers[worker_id]);
    if (remote_worker == NULL) {
        return -1;
    }
    
    batch->worker = remote_worker;
    batch->commit_callback = pz_batch_remote_commit;
    return 0;
}

void pz_batch_push(struct pz_batch* NOALIAS batch, struct pz_task* NOALIAS task) {
    struct pz_queue_node* node = &task->node;
    struct pz_worker* worker = batch->worker;

    if (LIKELY(batch->producer != 0)) {
        if (pz_queue_buffer_producer_push(&batch->producer, node, &worker->runnable)) {
            return;
        }
    }

    pz_queue_list_push(&batch->list, node);
}

struct pz_task* pz_batch_pop(struct pz_batch* batch) {
    struct pz_queue_node* node = pz_queue_list_pop(&batch->list);
    struct pz_worker* worker = batch->worker;

    if (UNLIKELY(node == NULL)) {
        if (LIKELY(batch->producer != 0)) {
            node = pz_queue_buffer_producer_pop(&batch->producer, &worker->runnable);
        }
    }
    
    return CONTAINER_OF(struct pz_task, node, node);
}

bool pz_batch_submit(struct pz_batch* batch) {
    return batch->commit_callback(batch);
}

bool pz_batch_local_commit(struct pz_batch* batch) {
    struct pz_worker* worker = batch->worker;
    return pz_queue_list_push_all(&worker->local, &batch->list);
}

bool pz_batch_shared_commit(struct pz_batch* batch) {
    struct pz_worker* worker = batch->worker;
    
    int submitted = pz_queue_buffer_producer_commit(&batch->producer, &worker->runnable);
    submitted |= pz_queue_injector_push(&worker->overflowed, &batch->list);
    if (LIKELY(submitted)) {
        pz_scheduler_unpark(&worker->scheduler);
    }

    return submitted;
}

bool pz_batch_remote_commit(struct pz_batch* batch) {
    struct pz_worker* worker = batch->worker;

    int submitted = pz_queue_injector_push(&worker->overflowed, &batch->list);
    if (LIKELY(submitted)) {
        pz_event_set(&worker->event);
    }

    return submitted;
}

#endif // _PZ_WORKER_H