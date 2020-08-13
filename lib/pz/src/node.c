#include "pz.h"
#include "worker.h"

#define IDLE_WAKING (1 << 0)
#define IDLE_NOTIFIED (1 << 1)
#define IDLE_SHUTDOWN UINTPTR_MAX

typedef struct IdleQueue {
    uintptr_t worker_index;
    uint8_t aba_tag;
    bool is_tagged;
} IdleQueue;

static PZ_INLINE IdleQueue IdleQueueFromPtr(uintptr_t idle_queue) {
    IdleQueue self;
    self.worker_index = idle_queue >> 8;
    self.aba_tag = idle_queue & (0xff >> 1);
    self.is_tagged = (idle_queue & 1) != 0;
    return self;
}

static PZ_INLINE uintptr_t IdleQueueToPtr(IdleQueue self) {
    uintptr_t idle_queue = 0;
    idle_queue |= self.worker_index << 8;
    idle_queue |= ((uintptr_t)self.aba_tag) << 1;
    idle_queue |= self.is_tagged ? 1 : 0;
    return idle_queue;
}

void PzTaskNodeInit(
    PZ_NOALIAS(PzTaskNode*) self,
    PZ_NOALIAS(PzTaskWorker*) workers,
    size_t num_workers
) {
    // setup the basic PzTaskNode fields
    self->next = self;
    self->scheduler = NULL;
    self->active_threads = 0;
    self->runq = (uintptr_t)NULL;
    
    // set the worker array and limit it to the max index
    self->workers = workers;
    self->num_workers = num_workers;
    if (self->num_workers > PZ_TASK_WORKER_MAX)
        self->num_workers = PZ_TASK_WORKER_MAX;

    // build the stack of off-by-one idle worker indices
    self->idle_queue = 0;
    for (size_t i = 0; i < self->num_workers; i++) {
        IdleQueue idle = IdleQueueFromPtr(self->idle_queue);
        self->workers[i].ptr = PzTaskWorkerInit(PZ_TASK_WORKER_IDLE, idle.worker_index << 8);
        idle.worker_index = i + 1;
        self->idle_queue = IdleQueueToPtr(idle);
    }
}

void PzTaskNodeDestroy(PzTaskNode* self) {
    // ensure that this node was shutdown
    uintptr_t idle_queue = PzAtomicLoad(&self->idle_queue);
    PzAssert(idle_queue == IDLE_SHUTDOWN, "node deinitialized when not shutdown");
    
    // ensure that the runq is empty
    uintptr_t runq = PzAtomicLoad(&self->runq);
    PzAssert(runq == (uintptr_t)NULL, "node deinitialized with non-empty run queue");

    // ensure that there are no active threads running
    uintptr_t active_threads = PzAtomicLoad(&self->active_threads);
    PzAssert(active_threads == 0, "node deinitialized with %zu active threads", active_threads);
}

void PzTaskThreadNodePush(PzTaskNode* self, PzTaskBatch batch);

void PzTaskNodePush(PzTaskNode* self, PzTaskBatch batch) {
    PzTaskThreadNodePush(self, batch);
}

void PzTaskNodeResumeThread(
    PZ_NOALIAS(PzTaskNode*) self,
    PZ_TASK_RESUME_TYPE resume_type,
    PZ_NOALIAS(PzTaskResumeResult*) resume_result,
    bool is_waking
) {
    PzDebugAssert(self != NULL, "invalid PzTaskNode ptr");
    PzDebugAssert(resume_result != NULL, "invalid PzTaskResumeResult ptr");

    uintptr_t idle_queue = PzAtomicLoadAcquire(&self->idle_queue);
    while (true) {
        PzAssert(idle_queue != IDLE_SHUTDOWN, "node shutdown when trying to resume");

        // TODO
    }
}

void PzTaskNodeResume(
    PZ_NOALIAS(PzTaskNode*) self,
    PZ_TASK_RESUME_TYPE resume_type,
    PZ_NOALIAS(PzTaskResumeResult*) resume_result
) {
    PzDebugAssert(self != NULL, "invalid PzTaskNode ptr");
    PzDebugAssert(resume_result != NULL, "invalid PzTaskResumeResult ptr");
    PzTaskNodeResumeThread(self, resume_type, resume_result, false);
}

void PzTaskNodeUndoResume(
    PZ_NOALIAS(PzTaskNode*) self,
    PZ_NOALIAS(PzTaskResumeResult*) result_ptr
) {
    PZ_UNREFERENCED_PARAMETER(self);
    PZ_UNREFERENCED_PARAMETER(result_ptr);

    // TODO

    return;
}

PZ_TASK_SUSPEND_STATUS PzTaskNodeSuspend(
    PZ_NOALIAS(PzTaskNode*) self,
    PZ_NOALIAS(PzTaskThread*) thread
) {
    PZ_UNREFERENCED_PARAMETER(self);
    PZ_UNREFERENCED_PARAMETER(thread);

    // TODO

    return (PZ_TASK_SUSPEND_STATUS) 0;
}
