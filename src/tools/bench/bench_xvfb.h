#ifndef BENCH_XVFB_H
#define BENCH_XVFB_H

#include <sys/types.h>

/* Returns 0 when no display was needed (a device with a real panel, or
 * DISPLAY already set), the child PID when Xvfb was spawned (kill it with
 * SIGTERM to tear it down), or -1 on failure. */
pid_t bench_xvfb_start_if_needed(int headless_device, int width, int height);

#endif
