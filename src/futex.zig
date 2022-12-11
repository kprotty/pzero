const std = @import("std");
const builtin = @import("builtin");
const os = std.os;
const assert = std.debug.assert;

pub usingnamespace switch (builtin.target.os.tag) {
    .linux => LinuxFutex,
    .windows => WindowsFutex,
    .netbsd => NetBSDFutex,
    .openbsd => OpenBSDFutex,
    .dragonfly => DragonflyFutex,
    .freebsd, .kfreebsd => FreeBSDFutex,
    .macos, .ios, .tvos, .watchos => DarwinFutex,
    else => UnsupportedFutex,
};

const UnsupportedFutex = @compileError("platform support for futex(2) not implemented");