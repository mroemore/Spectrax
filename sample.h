#include <stdio.h>
#ifndef SAMPLE_H
#define SAMPLE_H

typedef struct {
    float *data;
    int length;
    int sample_rate;
    int bit;
} Sample;

Sample load_raw_sample(const char *filename, int sample_rate);
Sample load_wav_sample(const char *filename, FILE *log_file);
void free_sample(Sample *sample);
float get_sample_value(Sample *sample, float *sample_position, float phase_increment, int pa_sr, int loop);
// float get_sample_value(Sample *sample, float *sample_position, int pa_sr, float phase);

#endif // SAMPLE_H