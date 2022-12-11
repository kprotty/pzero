const std = @import("std");
const atomic = @import("atomic.zig");

comptime { assert(@bitSizeOf(Sync) == @bitSizeOf(u32)); }
const Sync = packed struct {
    state: enum(u2) {
        pending,
        signaled,
        waking,
        shutdown,
    } = .pending,
    notified: bool = false,
    joining: bool = false,
    idle: u14 = 0,
    spawnable: u14 = 0,
};

pub const Idle = extern struct {
    sync: u32 = 0,
    stop: u32 = 0,

    fn notify(idle: *Idle, is_waking: bool) void {
        var sync = @bitCast(Sync, atomic.load(&idle.sync, .relaxed));
        while (sync.state != .shutdown) {
            var new_sync = sync;
            new_sync.notified = true;
            if (is_waking) assert(sync.state == .waking);

            const can_wake = is_waking or sync.state == .pending;
            if (can_wake and sync.idle > 0) {
                new_sync.state = .signaled;
            } else if (can_wake and sync.spawnable > 0) {
                new_sync.state = .signaled;
                new_sync.spawnable -= @boolToInt(sync.idle == 0);
            } else if (is_waking) {
                new_sync.state = .pending;
            } else if (sync.notified) {
                return;
            }

            sync = @bitCast(Sync, atomic.cas(
                .weak,
                &idle.sync,
                @bitCast(u32, sync),
                @bitCast(u32, new_sync),
                .release,
                .relaxed,
            ) orelse {
                if (can_wake and sync.idle > 0) {
                    return Platform.unpark(idle);
                }

                if (can_wake and sync.spawnable > 0) {
                    const stop = @bitCast(Stop, atomic.load(&idle.stop, .relaxed));
                    const spawn_id = sync.spawnable - stop.max_spawnable;
                    return Platform.spawn(idle, spawn_id) catch idle.complete();
                }

                return;
            });
        }
    }

    fn wait(idle: *Idle, is_waking: bool) bool {
        var sync = @bitCast(Sync, atomic.load(&idle.sync, .relaxed));

        while (true) {

        }
    }

    fn shutdown(idle: *Idle) void {

    }

    fn join(idle: *Idle) void {
        
    }
};