const std = @import("std");
const assert = std.debug.assert;
const atomic = @import("atomic.zig");
const random = @import("random.zig");

pub const Node = extern struct {
    next: ?*Node = null,
};

pub const List = extern struct {
    head: ?*Node = null,
    tail: ?*Node = null,

    pub fn from(node: *Node) List {
        node.next = null;
        return .{ .head = node, .tail = node };
    }

    pub fn push(list: *List, other: List) void {
        const prev = if (list.tail) |tail| &tail.next else &list.head;
        prev.* = other.head orelse return;
        list.tail = other.tail orelse unreachable;
    }

    pub fn pop(list: *List) ?*Node {
        const node = list.head orelse return null;
        list.head = node.next;
        if (list.head == null) list.tail = null;
        return node;
    }
};

pub const Injector = extern struct {
    tail: ?*Node = null,
    _padding: atomic.CachePadding(?*Node) = undefined,
    head: usize = 0,

    const popping_bit = 0b1;
    const node_mask = ~@as(usize, popping_bit);
    comptime { assert(@alignOf(Node) > ~node_mask); }

    pub fn push(noalias injector: *Injector, batch: Batch) void {
        const first = batch.head orelse return;
        const last = batch.tail orelse unreachable;
        assert(last.next == null);

        if (atomic.swap(&injector.tail, last, .acq_rel)) |tail| {
            atomic.store(&tail.next, first, .release);
            return;
        }

        const head = atomic.fetch_add(&injector.head, @ptrToInt(first), .release);
        assert(head & node_mask == 0);
    }

    const Guard = ?*Node;

    fn acquire(injector: *Injector) ?Guard {
        var head = atomic.load(&injector.head, .relaxed);
        while (true) {
            if (head & popping_bit != 0) return null; // acquired
            const guard = @intToPtr(?*Node, head & node_mask) orelse return null; // empty
            head = atomic.cas(.weak, &injector.head, head, popping_bit, .acquire, .relaxed) orelse return guard;
        }
    }

    fn pop(noalias injector: *Injector, noalias guard: *Guard) ?*Node {
        if (guard.* == null) {
            const head = atomic.load(&injector.head, .acquire);
            assert(head & popping_bit != 0);

            guard.* = @intToPtr(?*Node, head & node_mask) orelse return null;
            atomic.store(&injector.head, popping_bit, .relaxed);
        }

        const head = guard.* orelse unreachable;
        guard.* = atomic.load(&head.next, .acquire) orelse blk: {
            const tail = atomic.cas(.strong, &injector.tail, head, null, .acquire, .acquire) orelse break :blk null;
            assert(tail != null);

            atomic.yield();
            break :blk atomic.load(&head.next, .acquire) orelse return null;
        };

        return head;
    }

    fn release(noalias injector: *Injector, noalias guard: Guard) void {
        const new_head = guard orelse {
            const head = atomic.fetch_sub(&injector.head, popping_bit, .release);
            assert(head & popping_bit != 0);
            return;
        };

        if (std.debug.runtime_safety) {
            const head = atomic.load(&injector.head, .unordered);
            assert(head & popping_bit != 0);
        }

        atomic.store(&injector.head, @ptrToInt(new_head), .release);
    }
};

pub const Buffer = extern struct {
    head: u32 = 0,
    _padding: atomic.CachePadding(u32) = undefined,
    tail: u32 = 0,
    array: [capacity]?*Node = [_]?*Node{ null } ** capacity,

    const capacity = 256;
    comptime { assert(std.math.maxInt(u32) > capacity); }

    pub fn push(noalias buffer: *Buffer, noalias injector: *Injector, noalias node: *Node) void {
        const tail = buffer.tail;
        var head = atomic.load(&buffer.head, .relaxed);

        var size = tail -% head;
        assert(size <= capacity);

        if (size == capacity) overflow: {
            var migrate = size / 2;
            assert(migrate > 0);

            if (atomic.cas(.strong, &buffer.head, head +% migrate, .acquire, .relaxed)) |new_head| {
                size = tail -% new_head;
                break :overflow;
            }

            var overflowed = List{};
            while (migrate > 0) : (migrate -= 1) {
                const old_node = buffer.array[head % capacity] orelse unreachable;
                overflowed.push(List.from(old_node));
                head +%= 1;
            }

            overflowed.push(List.from(node));
            injector.push(overflowed);
            return;
        }

        assert(size < capacity);
        atomic.store(&buffer.array[tail % capacity], node, .unordered);
        atomic.store(&buffer.tail, tail +% 1, .release);
    }

    pub fn pop(buffer: *Buffer) ?*Node {
        // Preemptively bump the head to claim a pushed slot in the array.
        // acquire to ensure bumping the head happens-before claimed node reads/writes.
        const tail = buffer.tail;
        const head = atomic.fetch_add(&buffer.head, 1, .acquire);

        const size = tail -% head;
        assert(size <= capacity);

        // Buffer being empty means head is accidently before tail so need to fix it up.
        if (size == 0) {
            atomic.store(&buffer.head, head, .relaxed);
            return null;
        }

        const node = buffer.array[head % capacity] orelse unreachable;
        return node;
    }

    pub fn inject(noalias buffer: *Buffer, noalias injector: *Injector) ?*Node {
        var guard = injector.acquire() orelse return null;
        defer injector.release(guard);

        const tail = buffer.tail;
        const head = atomic.load(&buffer.head, .unordered);
        assert(tail == head); // should only inject if buffer is empty

        // Fill up our array with nodes popped from the injector.
        // Writes to the array must be atomic as it could be getting read from a steal() thread.
        var pushed: u32 = 0;
        while (pushed < capacity) : (pushed += 1) {
            const node = injector.pop(&guard) orelse break;
            atomic.store(&buffer.array[(tail +% pushed) % capacity], node, .unordered);
        }

        // We return the last node pushed, so update the tail only if it changed.
        // release to ensure other threads stealing load with acquire and see copied/written nodes. 
        pushed = std.math.sub(u32, pushed, 1) catch return null;
        if (pushed > 0) atomic.store(&buffer.tail, tail +% pushed, .release);

        const node = buffer.array[(tail +% pushed) % capacity] orelse unreachable;
        return node;
    }

    pub fn steal(noalias buffer: *Buffer, noalias rng: *random.Generator) ?*Node {
        var array = [_]?*Node{ null };

        // Steal only a single item from the buffer.
        const pushed = buffer.steal_into(&array, 0, rng);
        if (pushed == 0) return null;

        const node = array[0] catch unreachable;
        return node;
    }

    pub fn steal_from(noalias buffer: *Buffer, noalias target: *Buffer, noalias rng: *random.Generator) ?*Node {
        const tail = buffer.tail;
        const head = atomic.load(&buffer.head, .unordered);
        assert(tail == head); // should only steal if buffer is empty

        var pushed = target.steal_into(&buffer.array, tail, rng);
        if (pushed == 0) return null;

        // We return the last node pushed, so update the tail only if it changed.
        // release to ensure other threads stealing load with acquire and see copied/written nodes. 
        pushed -= 1;
        if (pushed > 0) atomic.store(&buffer.tail, tail +% pushed, .release);

        const node = buffer.array[(tail +% pushed) % capacity] orelse unreachable;
        return node;
    }

    fn steal_into(
        noalias buffer: *Buffer,
        noalias into_array: anytype,
        into_tail: u32,
        noalias rng: *random.Generator,
    ) u32 {
        while (true) {
            // acquire ensures head load happens-before tail load.
            // acquire ensures tail load happens-before released array writes.
            const head = atomic.load(&buffer.head, .acquire);
            const tail = atomic.load(&buffer.tail, .acquire);

            // Check the size as signed to account for pop() advancing 
            // the head by one even when we're empty with head == tail.
            const size = tail -% head;
            if (@bitCast(i32, size) <= 0) return 0;

            // Only be able to steal half of the items to amortize pop() turning into steal().
            // If the amount to steal is over half the capacity,
            // a lot of pushes happened between head and tail load so retry.
            const stealable = std.math.max(1, size / 2);
            if (stealable > (capacity / 2)) {
                atomic.yield();
                continue;
            }

            // Only steal enough to fill the into_buffer
            const into_capacity = @intCast(u32, into_buffer.len);
            const to_steal = std.math.min(into_capacity, stealable);

            // Copy the nodes from the buffer to into_array.
            // The copying must be done atomically to avoid data races
            // as the buffer could be writing to the same slots if the tail wraps around.
            var pushed: u32 = 0;
            while (pushed < to_steal) : (pushed += 1) {
                const buffer_index = (head +% pushed) % capacity;
                const into_index = (into_tail +% pushed) % into_capacity;

                const node = atomic.load(&buffer.array[buffer_index], .unordered) orelse unreachable;
                atomic.store(&into_array[into_index], node, .unordered);
            }

            // acq_rel implies release to ensure node copying happens-before bumping head which makes slots reusable.
            // acq_rel implies acquire to ensure bumping head happens-before stolen node usage.
            // Retry the entire steal process using an atomic backoff to try and amortize CAS contention.
            if (atomic.cas(.strong, &buffer.head, head, head +% to_steal, .acq_rel, .relaxed)) |_| {
                atomic.backoff(rng);
                continue;
            }

            return to_steal;
        }
    }
};