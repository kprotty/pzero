const std = @import("std");
const builtin = @import("builtin");
const random = @import("random.zig");

const is_apple_aarch64 = builtin.target.os.tag.isDarwin() and builtin.taregt.cpu.arch == .aarch64;

pub fn CachePadding(comptime T: type) type {
    const aligned = std.mem.alignForward(@sizeOf(T), std.atomic.cache_line);
    const padding = aligned - @sizeOf(T);
    return [padding]u8;
}

pub inline fn yield() void {
    // On Apple bionic and M1 chips, "WFE" yields CPU the best when handling contention.
    if (is_apple_aarch64) {
        return asm volatile("wfe" ::: "memory");
    }

    // _mm_pause on x86 and the equivalents on other platforms.
    return std.atomic.spinLoopHint();
}

pub fn backoff(rng: *random.Generator) void {
    // On Apple bionic and M1 chips, "WFE" yields CPU the best when handling contention.
    if (is_apple_aarch64) {
        return yield();
    }

    // Use randomness to prevent threads from continuously contending at the same frequency:
    // https://github.com/apple/swift-corelibs-libdispatch/blob/469c8ecfa0011f5da23acacf8658b6a60a899a78/src/shims/yield.h#L91-L125
    const spins_max = 128 - 1;
    const spins_min = 32 - 1;

    // Shift by 24 to sample the highest bits from the RNG as they often contain the most entropy.
    var spins = ((rng.next() >> 24) & spins_max) | spins_min;
    while (true) {
        spins = std.math.sub(u32, spins, 1) catch return;
        yield();
    }
}

pub const Ordering = enum {
    unordered,
    relaxed,
    acquire,
    release,
    acq_rel,
    seq_cst,

    inline fn to_builtin(comptime ordering: Ordering) builtin.AtomicOrder {
        return switch (ordering) {
            .unordered => .Unordered,
            .relaxed => .Monotonic,
            .acquire => .Acquire,
            .release => .Release,
            .acq_rel => .AcqRel,
            .seq_cst => .SeqCst,
        };
    }
};

pub inline fn load(ptr: anytype, comptime ordering: Ordering) @TypeOf(ptr.*) {
    return @atomicLoad(@TypeOf(ptr.*), ptr, comptime ordering.to_builtin());
}

pub inline fn store(ptr: anytype, value: @TypeOf(ptr.*), comptime ordering: Ordering) void {
    return @atomicStore(@TypeOf(ptr.*), ptr, value, comptime ordering.to_builtin());
}

pub inline fn swap(ptr: anytype, value: @TypeOf(ptr.*), comptime ordering: Ordering) @TypeOf(ptr.*) {
    return @atomicRmw(@TypeOf(ptr.*), ptr, .Xchg, value, comptime ordering.to_builtin());
}

pub inline fn fetch_add(ptr: anytype, value: @TypeOf(ptr.*), comptime ordering: Ordering) @TypeOf(ptr.*) {
    return @atomicRmw(@TypeOf(ptr.*), ptr, .Add, value, comptime ordering.to_builtin());
}

pub inline fn fetch_sub(ptr: anytype, value: @TypeOf(ptr.*), comptime ordering: Ordering) @TypeOf(ptr.*) {
    return @atomicRmw(@TypeOf(ptr.*), ptr, .Sub, value, comptime ordering.to_builtin());
}

pub const Strictness = enum {
    strong,
    weak,
};

pub inline fn cas(
    comptime strictness: Strictness,
    ptr: anytype,
    cmp: @TypeOf(ptr.*),
    xchg: @TypeOf(ptr.*),
    comptime success_ordering: Ordering,
    comptime failure_ordering: Ordering,
) ?@TypeOf(ptr.*) {
    const success = comptime success_ordering.to_builtin();
    const failure = comptime failure_ordering.to_builtin();
    return switch (strictness) {
        .strong => @cmpxchgStrong(@TypeOf(ptr.*), ptr, cmp, xchg, success, failure),
        .weak => @cmpxchgWeak(@TypeOf(ptr.*), ptr, cmp, xchg, success, failure),
    };
}