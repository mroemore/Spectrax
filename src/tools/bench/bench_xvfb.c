#include "bench_xvfb.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

pid_t bench_xvfb_start_if_needed(int headless_device, int width, int height) {
    /* A device has a real panel: never spawn an X server there. */
    if (headless_device) return 0;
    if (getenv("DISPLAY") != NULL) return 0;
    if (system("command -v Xvfb >/dev/null 2>&1") != 0) {
        fprintf(stderr, "error: DISPLAY unset and Xvfb not on PATH; install xorg-server-Xvfb\n");
        return -1;
    }
    if (width <= 0) width = 640;
    if (height <= 0) height = 480;
    char screen[64];
    snprintf(screen, sizeof screen, "%dx480x24", width);
    pid_t pid = fork();
    if (pid == 0) {
        execlp("Xvfb", "Xvfb", ":97", "-screen", "0", screen, "-nolisten", "tcp",
               (char *)NULL);
        _exit(127);
    }
    if (pid < 0) return -1;
    sleep(1);
    setenv("DISPLAY", ":97", 1);
    return pid;
}
