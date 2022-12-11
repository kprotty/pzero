const std = @import("std");
const assert = std.debug.assert;

/// Fast, non-cryptographic, pseudo-random number generator.
pub const Generator = extern struct {
    state: u32,

    pub fn init(seed: u32) Generator {
        assert(seed != 0);
        return .{ .state = seed };
    }

    // Old BSD rand() that's a "fast enough" LCG: 
    // https://rosettacode.org/wiki/Random_number_generator_(included)#C
    // https://github.com/openbsd/src/blob/1c0cb70272810e48ba7913cd0edee58b5f0963c0/lib/libc/stdlib/rand.c#L37-L42
    //
    // Callers are responsible for extracting out the high bits with the most entropy.
    pub fn next(gen: *Generator) u32 {
        const state = gen.state;
        gen.state = (state *% 1103515245) +% 12345;
        return state;
    }
};

/// Iterate all values in a range exactly one in a "random order":
/// https://lemire.me/blog/2017/09/18/visiting-all-values-in-an-array-exactly-once-in-random-order/
pub const Sequence = extern struct {
    iter: u32,
    range: u32,
    index: u32,

    pub fn init(seed: u32, range: u32) Sequence {
        assert(seed != 0);
        assert(range != 0);
        return .{
            .iter = range,
            .index = seed % range,
            .range = range,
        };
    }

    pub fn next(seq: *Sequence) ?u32 {
        seq.iter = std.math.sub(u32, seq.iter, 1) catch return null;

        defer {
            // range - 1 seems to always be a valid co-prime from testing.
            const co_prime = seq.range - 1;
            seq.index += co_prime;

            if (seq.index >= seq.range) seq.index -= seq.range;
            assert(seq.index < seq.range);
        }

        return seq.index;
    }
};