/*
 * test_bench_report.c — roundtrip test for the JSON+CSV writer.
 *
 * Build: meson test -C build test_bench_report
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bench_report.h"
#include "cJSON.h"

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); exit(1); } \
} while (0)

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *buf = malloc(n + 1);
    fread(buf, 1, n, f);
    buf[n] = 0;
    fclose(f);
    return buf;
}

static int test_roundtrip(void) {
    char tmpl[] = "/tmp/spectrax_bench_XXXXXX";
    char *out = mkdtemp(tmpl);
    ASSERT_TRUE(out != NULL, "mkdtemp");

    BenchReport *r = bench_report_new(out);
    bench_report_str(r, "meta", "arch", "aarch64");
    bench_report_metric(r, "audio", "callback_ms_avg", 0.42);
    bench_report_metric(r, "audio", "callback_ms_p99", 1.1);
    bench_report_str(r, "audio", "voices", "8");

    const char *cols[] = {"t_ms", "callback_ms"};
    bench_report_series_begin(r, "audio_callbacks", cols, 2);
    double row1[] = {0.0, 0.41};
    double row2[] = {2.67, 0.43};
    bench_report_series_row(r, "audio_callbacks", row1);
    bench_report_series_row(r, "audio_callbacks", row2);
    bench_report_series_end(r);
    bench_report_log(r, "hello %d\n", 42);

    ASSERT_TRUE(bench_report_flush(r) == 0, "flush");
    bench_report_free(r);

    char path[1024];
    snprintf(path, sizeof path, "%s/summary.json", out);
    char *json = slurp(path);
    cJSON *root = cJSON_Parse(json);
    ASSERT_TRUE(root != NULL, "json parses");
    cJSON *audio = cJSON_GetObjectItemCaseSensitive(root, "audio");
    ASSERT_TRUE(audio != NULL, "audio section");
    cJSON *avg = cJSON_GetObjectItemCaseSensitive(audio, "callback_ms_avg");
    ASSERT_TRUE(cJSON_IsNumber(avg) && fabs(avg->valuedouble - 0.42) < 1e-9, "avg matches");
    cJSON *p99 = cJSON_GetObjectItemCaseSensitive(audio, "callback_ms_p99");
    ASSERT_TRUE(cJSON_IsNumber(p99) && fabs(p99->valuedouble - 1.1) < 1e-9, "p99 matches");
    cJSON_Delete(root);
    free(json);

    snprintf(path, sizeof path, "%s/timeseries.csv", out);
    char *csv = slurp(path);
    ASSERT_TRUE(strstr(csv, "# series: audio_callbacks") != NULL, "series header");
    ASSERT_TRUE(strstr(csv, "t_ms,callback_ms") != NULL, "column names");
    ASSERT_TRUE(strstr(csv, "0.000000,0.410000") != NULL, "row 1");
    ASSERT_TRUE(strstr(csv, "2.670000,0.430000") != NULL, "row 2");
    free(csv);

    snprintf(path, sizeof path, "%s/console.log", out);
    struct stat st;
    ASSERT_TRUE(stat(path, &st) == 0 && st.st_size > 0, "console.log exists+non-empty");

    snprintf(path, sizeof path, "%s/hostinfo.txt", out);
    ASSERT_TRUE(stat(path, &st) == 0, "hostinfo.txt exists");

    return 0;
}

static int test_device_stamp(void) {
    char tmpl[] = "/tmp/spectrax_bench_XXXXXX";
    char *out = mkdtemp(tmpl);
    ASSERT_TRUE(out != NULL, "mkdtemp");

    BenchReport *r = bench_report_new(out);
    bench_report_str(r, "meta", "device_id", "rg35xxsp");
    bench_report_str(r, "meta", "device", "Anbernic RG35XX SP");
    bench_report_metric(r, "meta", "display_width", 640);
    bench_report_metric(r, "meta", "display_height", 480);
    ASSERT_TRUE(bench_report_flush(r) == 0, "flush");
    bench_report_free(r);

    char path[1024];
    snprintf(path, sizeof path, "%s/summary.json", out);
    char *json = slurp(path);
    cJSON *root = cJSON_Parse(json);
    ASSERT_TRUE(root != NULL, "json parses");
    cJSON *meta = cJSON_GetObjectItemCaseSensitive(root, "meta");
    ASSERT_TRUE(meta != NULL, "meta section");
    cJSON *id = cJSON_GetObjectItemCaseSensitive(meta, "device_id");
    ASSERT_TRUE(cJSON_IsString(id) && strcmp(id->valuestring, "rg35xxsp") == 0,
                "device_id stamp");
    cJSON *w = cJSON_GetObjectItemCaseSensitive(meta, "display_width");
    ASSERT_TRUE(cJSON_IsNumber(w) && w->valuedouble == 640, "display_width stamp");
    cJSON_Delete(root);
    free(json);

    return 0;
}

int main(void) {
    test_roundtrip();
    test_device_stamp();
    printf("test_bench_report: ok\n");
    return 0;
}
