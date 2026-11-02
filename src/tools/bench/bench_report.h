#ifndef BENCH_REPORT_H
#define BENCH_REPORT_H

#include <signal.h>

typedef struct BenchReport BenchReport;

BenchReport *bench_report_new(const char *out_dir);
void bench_report_free(BenchReport *r);

void bench_report_metric(BenchReport *r, const char *section, const char *key, double value);
void bench_report_str(BenchReport *r, const char *section, const char *key, const char *value);

void bench_report_series_begin(BenchReport *r, const char *name,
                               const char *const *col_names, int ncols);
void bench_report_series_row(BenchReport *r, const char *name, const double *values);
void bench_report_series_end(BenchReport *r);

void bench_report_log(BenchReport *r, const char *fmt, ...);
int bench_report_flush(BenchReport *r);

/* Set by the signal handler (SIGINT/SIGTERM/SIGHUP). The bench loops poll
 * it, so a report is still flushed when a suspend or a lid close kills
 * the run. */
extern volatile sig_atomic_t bench_stop_requested;

#endif
