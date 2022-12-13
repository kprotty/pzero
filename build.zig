const std = @import("std");

pub fn build(b: *std.build.Builder) void {
    const target = b.standardTargetOptions(.{});
    const mode = b.standardReleaseOptions();

    const pz = b.addSharedLibrary("pz", null, .unversioned);
    pz.addIncludePath("./src");
    pz.addCSourceFiles(
        &.{
            "src/queue.c",
        },
        &.{
            "-Wall",
            "-Wextra",
            "-Wpedantic",
        },
    );

    pz.setTarget(target);
    pz.setBuildMode(mode);
    pz.linkLibC();
    pz.install();
}