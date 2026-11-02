#ifndef BENCH_VOICETYPE_H
#define BENCH_VOICETYPE_H

#include "bench_options.h"
#include "bench_report.h"

/* Measures the per-voice synthesis cost of each VoiceType (SAMPLE, FM, BLEP,
 * GRAIN, SPECTRAL) through the same mixer loop the audio bench uses, and
 * reports how many voices of each type fit inside one callback deadline. */
int run_bench_voicetype(BenchReport *r, const BenchOptions *o);

#endif
