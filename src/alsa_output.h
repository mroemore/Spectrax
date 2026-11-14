#ifndef ALSA_OUTPUT_H
#define ALSA_OUTPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio.h"

/* Direct ALSA output: the device's own clock for the engine.
 *
 * PortAudio's ALSA path cannot drive the RG35XX SP's audio: on hw:0,0 it
 * substitutes 48000 for 192000 and never delivers a callback, and the
 * PipeWire-backed PCMs deliver callbacks far *faster* than real time
 * (measured 87,293 frames/s on spx48000 and 436,804/s on pipewire, against
 * 48,000 wanted). The sequencer counts steps in rendered samples, so the
 * callback is the song's clock: a stream that free-runs makes the song stall
 * and then race, and the audio is whatever survives the queue.
 *
 * A blocking snd_pcm_writei loop -- what aplay does -- paces correctly on the
 * same hardware (RATE 8000..192000, so 48000 needs no resampling), so this
 * module owns the PCM and uses its blocking write as the clock. The engine
 * still renders exactly `frameCount` samples per write, so the existing
 * sample-accurate step logic is unchanged; only the pacing comes from the
 * device instead of from PortAudio's scheduler. */

/* Opens `pcm` (e.g. "hw:0,0"), verifies the hardware really runs at
 * SPECTRAX_ENGINE_RATE, and starts the render thread. `frameCount` is the
 * engine buffer (the old framesPerBuffer). Returns false with a message in
 * err/errLen when the PCM cannot be used; nothing is started in that case. */
bool alsaOutputStart(paTestData *data, const char *pcm, unsigned frameCount,
                     char *err, size_t errLen);

void alsaOutputStop(void);
bool alsaOutputActive(void);

typedef struct {
	unsigned long writes;
	unsigned long frames;
	unsigned long xruns;
	unsigned long errors;
} AlsaOutputStats;

void alsaOutputStats(AlsaOutputStats *out);

/* The negotiated rate, or 0 before start. Always SPECTRAX_ENGINE_RATE when the
 * start succeeded: the rate is verified, not assumed. */
int alsaOutputRate(void);

/* The period/buffer the PCM ended up with, in frames; 0 before start. The
 * period is the engine's buffer and therefore the output latency. */
unsigned long alsaOutputPeriodFrames(void);
unsigned long alsaOutputBufferFrames(void);

/* float [-1,1] -> interleaved S32_LE, clamped. The codec accepts S16_LE and
 * S32_LE only (no float), so this conversion is on the audio path. Exposed for
 * the test. `samples` counts frames times channels. */
void alsaOutputConvertFloatToS32(const float *in, int32_t *out, size_t samples);

/* Same for the S16_LE fallback (a PCM that refuses S32). */
void alsaOutputConvertFloatToS16(const float *in, int16_t *out, size_t samples);

#endif
