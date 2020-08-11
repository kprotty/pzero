#include "pz.h"

void PzTaskThreadInit(PzTaskThread* self, PzTaskWorker* worker) {
    PZ_UNREFERENCED_PARAMETER(self);
    PZ_UNREFERENCED_PARAMETER(worker);

    // TODO

    return;
}

void PzTaskThreadDestroy(PzTaskThread* self) {
    PZ_UNREFERENCED_PARAMETER(self);

    // TODO

    return;
}

void PzTaskThreadSchedule(PzTaskThread* self, PzTaskBatch batch, PzTaskResumeResult* resume_ptr) {
    PZ_UNREFERENCED_PARAMETER(self);
    PZ_UNREFERENCED_PARAMETER(batch);
    PZ_UNREFERENCED_PARAMETER(resume_ptr);

    // TODO

    return;
}

PzTask* PzTaskThreadPoll(PzTaskThread* self, PzTaskPollPtr poll_ptr) {
    PZ_UNREFERENCED_PARAMETER(self);
    PZ_UNREFERENCED_PARAMETER(poll_ptr);

    // TODO

    return NULL;
}

PZ_TASK_SUSPEND_STATUS PzTaskNodeSuspend(PzTaskNode* self, PzTaskThread* thread);

PZ_TASK_SUSPEND_STATUS PzTaskThreadSuspend(PzTaskThread* self) {
    PzTaskNode* node = PzTaskThreadGetNode(self);
    return PzTaskNodeSuspend(node, self);
}
