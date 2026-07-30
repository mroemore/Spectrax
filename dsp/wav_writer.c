#include "wav_writer.h"

#include <stdio.h>
#include <string.h>

static void write_u32_le(unsigned char *buf, unsigned int v) {
    buf[0] = (unsigned char)(v & 0xff);
    buf[1] = (unsigned char)((v >> 8) & 0xff);
    buf[2] = (unsigned char)((v >> 16) & 0xff);
    buf[3] = (unsigned char)((v >> 24) & 0xff);
}

static void write_u16_le(unsigned char *buf, unsigned short v) {
    buf[0] = (unsigned char)(v & 0xff);
    buf[1] = (unsigned char)((v >> 8) & 0xff);
}

int wav_write(const char *path, const short *samples, size_t num_samples) {
    if (path == NULL) {
        return 1;
    }
    if (num_samples > 0 && samples == NULL) {
        return 2;
    }

    const unsigned int data_bytes = (unsigned int)(num_samples * (WAV_BITS_PER_SAMPLE / 8) * WAV_CHANNELS);
    const unsigned int file_bytes = 36u + data_bytes;

    unsigned char header[WAV_HEADER_SIZE];
    memset(header, 0, sizeof(header));

    /* RIFF chunk descriptor */
    memcpy(header + 0,  "RIFF", 4);
    write_u32_le(header + 4,  file_bytes);
    memcpy(header + 8,  "WAVE", 4);

    /* fmt subchunk */
    memcpy(header + 12, "fmt ", 4);
    write_u32_le(header + 16, 16);                 /* Subchunk1Size for PCM */
    write_u16_le(header + 20, 1);                  /* AudioFormat: PCM */
    write_u16_le(header + 22, WAV_CHANNELS);
    write_u32_le(header + 24, WAV_SAMPLE_RATE);
    write_u32_le(header + 28, WAV_SAMPLE_RATE * WAV_CHANNELS * (WAV_BITS_PER_SAMPLE / 8));
    write_u16_le(header + 32, WAV_CHANNELS * (WAV_BITS_PER_SAMPLE / 8));
    write_u16_le(header + 34, WAV_BITS_PER_SAMPLE);

    /* data subchunk */
    memcpy(header + 36, "data", 4);
    write_u32_le(header + 40, data_bytes);

    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return 3;
    }

    if (fwrite(header, 1, sizeof(header), fp) != sizeof(header)) {
        fclose(fp);
        return 4;
    }
    if (num_samples > 0) {
        const size_t sample_bytes = (size_t)num_samples * (WAV_BITS_PER_SAMPLE / 8);
        if (fwrite(samples, 1, sample_bytes, fp) != sample_bytes) {
            fclose(fp);
            return 5;
        }
    }

    if (fclose(fp) != 0) {
        return 6;
    }
    return 0;
}