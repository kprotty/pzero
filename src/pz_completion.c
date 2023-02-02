#include "pz_completion.h"

bool pz_cancel(pz_completion* completion) {
    bool cancelled;
    (((pz_completion_header*)completion)->impl)(completion, PZ_COMPLETION_REQUEST_CANCEL, (void*)&cancelled);
    return cancelled;
}

int pz_result(const pz_completion* completion) {
    int result;
    (((pz_completion_header*)completion)->impl)(completion, PZ_COMPLETION_REQUEST_RESULT, (void*)&result);
    return result;
}