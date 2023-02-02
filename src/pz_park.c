#include "pz_completion.h"
#include "pz_pending.h"
#include "pz_event.h"

typedef enum {
    PZ_PARK_IDLE,
    PZ_PARK_WAITING,
    PZ_PARK_CANCELED,
    PZ_PARK_NOTIFIED,
} PZ_PARK_STATE;

typedef union pz_park_node {
    struct {
        pz_completion_header header;
        void* key;
        _Atomic(uintptr_t) state;
        pz_pending_node cancel_link;
        union {
            struct {
                pz_pending_node link;
                void* context;
                pz_validate_callback validate;
            } insert;
            struct {
                union pz_park_node* children[2];
                union pz_park_node* next;
            } root;
            struct {
                union pz_park_node* prev;
                union pz_park_node* next;
            } queued;
        } data;
    } waiter;
    struct {
        void* key;
        void* context;
        pz_validate_callback validate;
        pz_event event;
    } waker;
} pz_park_node;

pz_static_assert(sizeof(pz_park_node) <= sizeof(pz_completion));

int pz_park_completion_request_impl(pz_completion* restrict completion, PZ_COMPLETION_REQUEST request) {
    pz_park_node* node = (pz_park_node*)completion;

}
