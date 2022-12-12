#ifndef _PZ_RANDOM_H
#define _PZ_RANDOM_H

#include "builtin.h"

struct pz_random {
    uint32_t state;
};

static void pz_random_init(struct pz_random* rng, uint32_t seed) {
    ASSERT(seed != 0, "random seed cannot be zero");
    rng->state = seed;
}

static FORCE_INLINE uint32_t pz_random_next(struct pz_random* rng) {
    ASSUME(rng != 0);
    
    uint32_t xorshift = rng->state;
    xorshift ^= xorshift << 13;
    xorshift ^= xorshift >> 17;
    xorshift ^= xorshift << 5;
    ASSUME(xorshift != 0);

    rng->state = xorshift;
    return xorshift;
}

struct pz_random_sequence {
    uint32_t iter;
    uint32_t range;
    uint32_t index;
};

static void pz_random_sequence_init(struct pz_random_sequence* NOALIAS seq, struct pz_random* NOALIAS rng, uint32_t range) {
    ASSUME(seq != NULL);
    ASSUME(rng != NULL);

    ASSERT(range > 0, "empty sequence range");
    ASSERT(range < (1 << 16), "sequence range too wide");

    seq->iter = range;
    seq->range = range;
    seq->index = (pz_random_next(rng) >> 16) % range;
}

static FORCE_INLINE bool pz_random_sequence_next(struct pz_random_sequence* seq) {
    bool valid = CHECKED_SUB(seq->iter, 1, &seq->iter);
    if (LIKELY(valid)) {
        uint32_t range = seq->range;
        uint32_t co_prime = range - 1;

        uint32_t index = seq->index + co_prime;
        if (index >= range) {
            index -= range;
        }

        ASSUME(index < range);
        seq->index = index;
    }

    return valid;
}


#endif // _PZ_RANDOM_H