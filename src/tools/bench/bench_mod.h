#ifndef BENCH_MOD_H
#define BENCH_MOD_H

#include "bench_options.h"
#include "bench_report.h"

/* Measures the per-buffer cost of a modulation graph (envelopes, stages,
 * LFOs, routes) so a patch's routing complexity can be budgeted against the
 * real-time deadline. */
int run_bench_mod(BenchReport *r, const BenchOptions *o);

#endif
