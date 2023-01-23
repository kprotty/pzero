const std = @import("std");

pub fn build(b: *std.build.Builder) void {
    const target = b.standardTargetOptions(.{});
    const mode = b.standardReleaseOptions();

    const pz = b.addSharedLibrary("pz", null, .unversioned);
    pz.addIncludePath("./src");
    pz.addIncludePath("./include");
    pz.addCSourceFiles(
        &.{
            "src/pz_event.c",
            "src/pz_reactor.c",
            "src/pz_runnable.c",
            "src/pz_scheduler.c",
            "src/pz_thread.c",
            "src/pz_time.c",
            //"src/pz_worker.c",
        },
        &.{
            "-Wall",
            "-Werror",
            "-Wextra",
            "-Wpedantic",
        },
    );

    pz.setTarget(target);
    pz.setBuildMode(mode);
    pz.linkLibC();
    pz.install();
}