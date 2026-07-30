#ifndef DSP_WAV_WRITER_H
#define DSP_WAV_WRITER_H

#include <stddef.h>

#define WAV_SAMPLE_RATE 44100
#define WAV_BITS_PER_SAMPLE 16
#define WAV_CHANNELS 1
#define WAV_HEADER_SIZE 44

/*
 * Write a mono 16-bit PCM WAV file.
 * samples: pointer to signed 16-bit PCM samples (native endian, will be
 *          written little-endian). May be NULL only if num_samples == 0.
 * num_samples: number of samples (frames) to write.
 * path: filesystem path to create. Must not be NULL.
 *
 * Returns 0 on success, non-zero on I/O error.
 */
int wav_write(const char *path, const short *samples, size_t num_samples);

#endif