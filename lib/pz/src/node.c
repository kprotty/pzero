#include "pz.h"
#include "worker.h"

#define IDLE_WAKING (1 << 0)
#define IDLE_NOTIFIED (1 << 1)
#define IDLE_SHUTDOWN UINTPTR_MAX

void PzTaskNodeInit(PzTaskNode* self, PzTaskWorker* workers, PzTaskWorkerCount num_workers) {
    // setup the basic PzTaskNode fields
    self->next = self;
    self->scheduler = NULL;
    self->active_threads = 0;
    self->runq = (uintptr_t)NULL;
    
    // set the worker array and limit it to the max index
    self->workers = workers;
    self->num_workers = num_workers;
    if (self->num_workers == UINT16_MAX)
        self->num_workers--;

    // build the stack of off-by-one idle worker indices
    self->idle_queue = 0;
    for (PzTaskWorkerIndex i = 0; i < self->num_workers; i++) {
        PzTaskWorkerIndex next_index = (PzTaskWorkerIndex) (self->idle_queue >> 16) ;
        self->workers[i].ptr = PzTaskWorkerInit(PZ_TASK_WORKER_IDLE, next_index << 2);
        self->idle_queue = ((uintptr_t)(i + 1)) << 16;
    }
}

void PzTaskNodeDestroy(PzTaskNode* self) {
    // ensure that this node was shutdown
    uintptr_t idle_queue = PzAtomicLoad(&self->idle_queue);
    PzAssert(idle_queue == IDLE_SHUTDOWN, "node deinitialized when not shutdown");
    
    // ensure that the runq is empty
    uintptr_t runq = PzAtomicLoad(&self->runq);
    PzAssert(runq == (uintptr_t)NULL, "node deinitialized with non-empty run queue")

    // ensure that there are no active threads running
    uintptr_t active_threads = PzAtomicLoad(&self->active_threads);
    PzAssert(active_threads == 0, "node deinitialized with %zu active threads", active_threads);
}

void PzTaskThreadNodePush(PzTaskNode* self, PzTaskBatch batch);

void PzTaskNodePush(PzTaskNode* self, PzTaskBatch batch) {
    PzTaskThreadNodePush(self, batch);
}

// TODO: bound wake-up to PzTaskThreadPoll() succes or PzTaskThreadSuspend() success
void PzTaskNodeResume(PzTaskNode* self, PzTaskResumeResult* result) {
    PzDebugAssert(self != NULL, "invalid PzTaskNode ptr");
    PzDebugAssert(result != NULL, "invalid PzTaskResumeResult ptr");

    uintptr_t idle_queue = PzAtomicLoadAcquire(&self->idle_queue);
    while (true) {
        PzAssert(idle_queue != IDLE_SHUTDOWN, "node shutdown when trying to resume");

        // TODO
    }
}

void PzTaskNodeUndoResume(PzTaskNode* self, PzTaskResumeResult* result_ptr) {
    PZ_UNREFERENCED_PARAMETER(self);
    PZ_UNREFERENCED_PARAMETER(result_ptr);

    // TODO

    return;
}

PZ_TASK_SUSPEND_STATUS PzTaskNodeSuspend(PzTaskNode* self, PzTaskThread* thread) {
    PZ_UNREFERENCED_PARAMETER(self);
    PZ_UNREFERENCED_PARAMETER(thread);

    // TODO

    return (PZ_TASK_SUSPEND_STATUS) 0;
}
