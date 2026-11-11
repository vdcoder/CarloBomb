/* Monotonic wall-clock time in seconds, for Linux/POSIX and Windows. */
#ifndef CARLOBOMB_TIMER_H
#define CARLOBOMB_TIMER_H

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static double cb_now(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}
#else
#include <time.h>
static double cb_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
#endif

#endif
