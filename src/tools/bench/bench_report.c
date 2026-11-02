#include "bench_report.h"
#include "cJSON.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define MAX_SERIES 8

struct BenchReport {
    char out_dir[512];
    cJSON *summary;
    FILE *csv;
    FILE *console_log;
    struct {
        char name[128];
        int ncols;
        char cols[16][64];
        int open;
    } series[MAX_SERIES];
    int n_series;
    int flushed;
};

static void ensure_dir(const char *path) {
    char cmd[1024];
    snprintf(cmd, sizeof cmd, "mkdir -p '%s'", path);
    if (system(cmd) != 0) { /* ignore */ }
}

static void write_hostinfo(const char *out_dir) {
    char path[1024];
    snprintf(path, sizeof path, "%s/hostinfo.txt", out_dir);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "captured_at: %ld\n", (long)time(NULL));
    FILE *src;
    char line[256];
    const char *files[] = {"/proc/cpuinfo", "/proc/meminfo", NULL};
    for (int i = 0; files[i]; i++) {
        fprintf(f, "\n=== %s ===\n", files[i]);
        src = fopen(files[i], "r");
        if (!src) { fprintf(f, "(unreadable)\n"); continue; }
        while (fgets(line, sizeof line, src)) fputs(line, f);
        fclose(src);
    }
    fclose(f);
}

BenchReport *bench_report_new(const char *out_dir) {
    ensure_dir(out_dir);
    BenchReport *r = calloc(1, sizeof *r);
    snprintf(r->out_dir, sizeof r->out_dir, "%s", out_dir);
    r->summary = cJSON_CreateObject();
    char path[1024];
    snprintf(path, sizeof path, "%s/timeseries.csv", out_dir);
    r->csv = fopen(path, "w");
    snprintf(path, sizeof path, "%s/console.log", out_dir);
    r->console_log = fopen(path, "w");
    write_hostinfo(out_dir);
    return r;
}

static cJSON *get_section(BenchReport *r, const char *section) {
    cJSON *s = cJSON_GetObjectItemCaseSensitive(r->summary, section);
    if (!s) { s = cJSON_AddObjectToObject(r->summary, section); }
    return s;
}

void bench_report_metric(BenchReport *r, const char *section, const char *key, double value) {
    if (!r || r->flushed) return;
    cJSON_AddNumberToObject(get_section(r, section), key, value);
}

void bench_report_str(BenchReport *r, const char *section, const char *key, const char *value) {
    if (!r || r->flushed) return;
    cJSON_AddStringToObject(get_section(r, section), key, value);
}

static int find_series(BenchReport *r, const char *name) {
    for (int i = 0; i < r->n_series; i++) {
        if (strcmp(r->series[i].name, name) == 0) return i;
    }
    return -1;
}

void bench_report_series_begin(BenchReport *r, const char *name,
                               const char *const *col_names, int ncols) {
    if (!r || r->flushed || !r->csv) return;
    if (r->n_series >= MAX_SERIES) return;
    if (find_series(r, name) >= 0) return;
    int idx = r->n_series++;
    snprintf(r->series[idx].name, sizeof r->series[idx].name, "%s", name);
    r->series[idx].ncols = ncols;
    for (int i = 0; i < ncols && i < 16; i++) {
        snprintf(r->series[idx].cols[i], sizeof r->series[idx].cols[i],
                 "%s", col_names[i]);
    }
    r->series[idx].open = 1;
    fprintf(r->csv, "# series: %s\n", name);
    for (int i = 0; i < ncols; i++) {
        fprintf(r->csv, "%s%s", i ? "," : "", col_names[i]);
    }
    fprintf(r->csv, "\n");
    fflush(r->csv);
}

void bench_report_series_row(BenchReport *r, const char *name, const double *values) {
    if (!r || r->flushed || !r->csv) return;
    int idx = find_series(r, name);
    if (idx < 0 || !r->series[idx].open) return;
    for (int i = 0; i < r->series[idx].ncols; i++) {
        fprintf(r->csv, "%s%.6f", i ? "," : "", values[i]);
    }
    fprintf(r->csv, "\n");
}

void bench_report_series_end(BenchReport *r) {
    if (!r || !r->csv) return;
    fprintf(r->csv, "\n");
    fflush(r->csv);
}

void bench_report_log(BenchReport *r, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (r && r->console_log) {
        va_start(ap, fmt);
        vfprintf(r->console_log, fmt, ap);
        va_end(ap);
        fflush(r->console_log);
    }
}

int bench_report_flush(BenchReport *r) {
    if (!r || r->flushed) return -1;
    char path[1024];
    snprintf(path, sizeof path, "%s/summary.json", r->out_dir);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    char *json = cJSON_Print(r->summary);
    fputs(json, f);
    fputc('\n', f);
    free(json);
    fclose(f);
    if (r->csv) fclose(r->csv);
    if (r->console_log) fclose(r->console_log);
    r->csv = NULL;
    r->console_log = NULL;
    r->flushed = 1;
    return 0;
}

void bench_report_free(BenchReport *r) {
    if (!r) return;
    if (!r->flushed) bench_report_flush(r);
    cJSON_Delete(r->summary);
    free(r);
}
