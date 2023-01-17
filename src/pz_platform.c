#ifdef _WIN32
    #include "pz_platform_windows.c"
#elif defined(__linux__)
    #include "pz_platform_linux.c"
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
    #include "pz_platform_bsd.c"
#else
    #error Operation system is not currently supported.
#endif