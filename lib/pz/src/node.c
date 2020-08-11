#include "pz.h"

void PzTaskNodeInit(PzTaskNode* self, PzTaskWorker* workers, PzTaskWorkerCount num_workers) {
    PZ_UNREFERENCED_PARAMETER(self);
    PZ_UNREFERENCED_PARAMETER(workers);
    PZ_UNREFERENCED_PARAMETER(num_workers);

    // TODO

    return;
}

void PzTaskNodeDestroy(PzTaskNode* self) {
    PZ_UNREFERENCED_PARAMETER(self);

    // TODO

    return;
}

bool PzTaskNodeResume(PzTaskNode* self, PzTaskResumeResult* result_ptr) {
    PZ_UNREFERENCED_PARAMETER(self);
    PZ_UNREFERENCED_PARAMETER(result_ptr);
    
    // TODO

    return false;
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
