#ifndef BENCH_OPTIONS_H
#define BENCH_OPTIONS_H

typedef struct {
    const char *bench;
    const char *out;
    const char *device;   /* device profile id, or NULL on the host */
    const char *workdir;  /* fileio working directory, or NULL */
    const char *gpu;      /* gles2 | gles3, or NULL */
    int audio_rate;       /* stream rate the device opened at; 0 = bench default */
    int width;            /* gfx window width */
    int height;           /* gfx window height */
    int duration;         /* seconds */
    int voices;
    int channels;
    int no_neon;
    unsigned seed;
} BenchOptions;

#endif
