const std = @import("std");

pub fn build(b: *std.build.Builder) void {
    const target = b.standardTargetOptions(.{});
    const mode = b.standardReleaseOptions();

    const pz = b.addSharedLibrary("pz", null, .unversioned);
    pz.addIncludePath("./src");
    pz.addIncludePath("./include");
    pz.addCSourceFiles(
        &.{
            "src/pz_queue.c",
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