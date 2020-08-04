const std = @import("std");

pub fn build(b: *std.build.Builder) void {
    const mode = b.standardReleaseOptions();
    const target = b.standardTargetOptions(.{});
    const libc = b.option(bool, "c", "Link libc") orelse false;

    const libpz = blk: {
        const lib = b.addSharedLibrary("pz", null, b.version(0, 1, 0));
        inline for ([_][]const u8{
            "pz",
        }) |c_source| {
            lib.addCSourceFile("./lib/pz/src/" ++ c_source ++ ".c", &[_][]const u8{
                "-Wall", "-Wpedantic", "-std=c11",
            });
        }

        if (libc) lib.linkLibC();
        lib.setBuildMode(mode);
        lib.setTarget(target);
        lib.setOutputDir("zig-cache");
        lib.addIncludeDir("./lib/pz/include");
        break :blk lib;
    };

    const pz_step = b.step("pz", "Build libzp shared library");
    pz_step.dependOn(&libpz.step);
}