/*
 * test_wav_writer.c — verify WAV header bytes and sample count.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wav_writer.h"

#define ASSERT_EQ(actual, expected) do { \
    if ((actual) != (expected)) { \
        fprintf(stderr, "FAIL %s:%d: expected %lld, got %lld\n", \
                __FILE__, __LINE__, (long long)(expected), (long long)(actual)); \
        return 1; \
    } \
} while (0)

#define ASSERT_BYTES(buf, offset, expected4) do { \
    if (memcmp((buf) + (offset), (expected4), 4) != 0) { \
        fprintf(stderr, "FAIL %s:%d: bytes at offset %d don't match '%c%c%c%c'\n", \
                __FILE__, __LINE__, (offset), \
                (expected4)[0], (expected4)[1], (expected4)[2], (expected4)[3]); \
        return 1; \
    } \
} while (0)

static int test_empty_wav(void) {
    const char *path = "test_empty.wav";
    const int rc = wav_write(path, NULL, 0);
    ASSERT_EQ(rc, 0);

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        fprintf(stderr, "could not open %s\n", path);
        return 1;
    }
    unsigned char header[WAV_HEADER_SIZE];
    if (fread(header, 1, sizeof(header), fp) != sizeof(header)) {
        fclose(fp);
        return 1;
    }
    fclose(fp);

    ASSERT_BYTES(header, 0, "RIFF");
    ASSERT_BYTES(header, 8, "WAVE");
    ASSERT_BYTES(header, 12, "fmt ");
    ASSERT_BYTES(header, 36, "data");

    /* File size (offset 4) should be 36 for an empty data chunk. */
    const unsigned int file_size =
        (unsigned int)header[4] |
        ((unsigned int)header[5] << 8) |
        ((unsigned int)header[6] << 16) |
        ((unsigned int)header[7] << 24);
    ASSERT_EQ(file_size, 36u);

    /* data subchunk size (offset 40) should be 0. */
    const unsigned int data_size =
        (unsigned int)header[40] |
        ((unsigned int)header[41] << 8) |
        ((unsigned int)header[42] << 16) |
        ((unsigned int)header[43] << 24);
    ASSERT_EQ(data_size, 0u);

    remove(path);
    printf("PASS test_empty_wav\n");
    return 0;
}

static int test_with_samples(void) {
    const char *path = "test_with_samples.wav";
    const short samples[] = { 0, 100, -100, 32767, -32768 };
    const size_t n = sizeof(samples) / sizeof(samples[0]);
    const int rc = wav_write(path, samples, n);
    ASSERT_EQ(rc, 0);

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return 1;
    }
    unsigned char header[WAV_HEADER_SIZE];
    if (fread(header, 1, sizeof(header), fp) != sizeof(header)) {
        fclose(fp);
        return 1;
    }
    short read_samples[8];
    if (fread(read_samples, sizeof(short), n, fp) != n) {
        fclose(fp);
        return 1;
    }
    fclose(fp);

    for (size_t i = 0; i < n; i++) {
        ASSERT_EQ(read_samples[i], samples[i]);
    }

    const unsigned int data_size =
        (unsigned int)header[40] |
        ((unsigned int)header[41] << 8) |
        ((unsigned int)header[42] << 16) |
        ((unsigned int)header[43] << 24);
    ASSERT_EQ(data_size, (unsigned int)(n * 2));

    remove(path);
    printf("PASS test_with_samples\n");
    return 0;
}

int main(void) {
    int failed = 0;
    failed |= test_empty_wav();
    failed |= test_with_samples();
    return failed;
}