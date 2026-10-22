/*
 * bench_fileio.c — sustained read/write benchmark for songs/settings/sample manifests.
 *
 * Writes and reads synthetic blobs at sizes 1 KB -> 8 MB, capturing
 * throughput and per-IO latency.
 *
 * The working directory matters: on a handheld /tmp is commonly a tmpfs, so
 * the default on-device measurement must target the real storage volume or
 * the numbers describe RAM rather than the SD card. That is what --workdir
 * (and the profile's paths.data_dir) is for.
 */
#include "bench_fileio.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static double seconds_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static double run_one(const char *dir, size_t bytes, int cycles, int do_write,
                      const char *series, BenchReport *r) {
    char path[1024];
    snprintf(path, sizeof path, "%s/blk_%zu.bin", dir, bytes);

    char *buf = malloc(bytes);
    if (!buf) return -1.0;
    for (size_t i = 0; i < bytes; i++) buf[i] = (char)(i * 131 + 17);

    double min_ms = 1e9, max_ms = 0, sum_ms = 0;
    for (int i = 0; i < cycles; i++) {
        double t0 = seconds_now();
        if (do_write) {
            int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { free(buf); return -1.0; }
            size_t left = bytes;
            char *p = buf;
            while (left) {
                ssize_t n = write(fd, p, left);
                if (n <= 0) break;
                left -= n; p += n;
            }
            close(fd);
        } else {
            int fd = open(path, O_RDONLY);
            if (fd < 0) { free(buf); return -1.0; }
            size_t left = bytes;
            char *p = buf;
            while (left) {
                ssize_t n = read(fd, p, left);
                if (n <= 0) break;
                left -= n; p += n;
            }
            close(fd);
        }
        double ms = (seconds_now() - t0) * 1000.0;
        if (ms < min_ms) min_ms = ms;
        if (ms > max_ms) max_ms = ms;
        sum_ms += ms;
        double row[3] = { (double)bytes, (double)i, ms };
        bench_report_series_row(r, series, row);
    }
    free(buf);

    double avg_ms = sum_ms / cycles;
    double throughput_mb_s = ((double)bytes / (1024.0 * 1024.0)) / (avg_ms / 1000.0);
    bench_report_metric(r, do_write ? "fileio_write" : "fileio_read",
                        "avg_ms_at_size", avg_ms);
    bench_report_metric(r, do_write ? "fileio_write" : "fileio_read",
                        "min_ms_at_size", min_ms);
    bench_report_metric(r, do_write ? "fileio_write" : "fileio_read",
                        "max_ms_at_size", max_ms);
    return throughput_mb_s;
}

int bench_fileio_run(const BenchOptions *opts, BenchReport *r) {
    const size_t sizes[] = { 1024, 16 * 1024, 256 * 1024, 4 * 1024 * 1024, 8 * 1024 * 1024 };
    const int cycles = 8;
    const char *cols[] = { "bytes", "cycle", "latency_ms" };

    char tmpl[512];
    char *dir = NULL;
    if (opts->workdir && opts->workdir[0]) {
        snprintf(tmpl, sizeof tmpl, "%s", opts->workdir);
        if (mkdir(tmpl, 0755) != 0 && errno != EEXIST) {
            /* The requested workdir (usually the device's data dir) is not
             * usable here. Fall back to a temp dir, but say so: the report
             * records the directory the numbers actually describe. */
            bench_report_log(r, "bench_fileio: workdir %s unusable (%s); "
                                "falling back to a temp dir\n", tmpl, strerror(errno));
            snprintf(tmpl, sizeof tmpl, "%s/spectrax_bench_fileio_XXXXXX",
                     getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
            if (!mkdtemp(tmpl)) {
                bench_report_log(r, "bench_fileio: mkdtemp failed\n");
                return 1;
            }
        }
        dir = tmpl;
    } else {
        snprintf(tmpl, sizeof tmpl, "%s/spectrax_bench_fileio_XXXXXX",
                 getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
        if (!mkdtemp(tmpl)) {
            bench_report_log(r, "bench_fileio: mkdtemp failed\n");
            return 1;
        }
        dir = tmpl;
    }

    bench_report_log(r, "bench_fileio: dir=%s cycles=%d\n", dir, cycles);
    bench_report_str(r, "fileio_write", "workdir", dir);
    bench_report_str(r, "fileio_read", "workdir", dir);

    bench_report_series_begin(r, "fileio_write", cols, 3);
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        double mbps = run_one(dir, sizes[i], cycles, 1, "fileio_write", r);
        char k[64]; snprintf(k, sizeof k, "size_%zu_mb_s", sizes[i]);
        bench_report_metric(r, "fileio_write", k, mbps);
    }
    bench_report_series_end(r);

    bench_report_series_begin(r, "fileio_read", cols, 3);
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        double mbps = run_one(dir, sizes[i], cycles, 0, "fileio_read", r);
        char k[64]; snprintf(k, sizeof k, "size_%zu_mb_s", sizes[i]);
        bench_report_metric(r, "fileio_read", k, mbps);
    }
    bench_report_series_end(r);

    return 0;
}
