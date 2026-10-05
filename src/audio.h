/* audio.h — the real-time render path, separated from main.c so a test can
 * drive it without a PortAudio stream.
 *
 * renderAudioBuffer() is the entire audio callback body. main.c's
 * patestCallback is a thin adapter that unpacks the PortAudio signature and
 * calls it. Tests call renderAudioBuffer directly, from a second thread, to
 * exercise the audio/GUI concurrency that the lock in that function exists to
 * make safe (see TESTING.md, "Dynamic-analysis matrix"). */
#ifndef SPECTRAX_AUDIO_H
#define SPECTRAX_AUDIO_H

#include <stdbool.h>

#include "main.h"

/* Render one buffer of framesPerBuffer stereo frames into out (which must hold
 * framesPerBuffer * 2 floats). Takes g_audioLock for the duration. Returns
 * nothing; a NULL data or out is a programming error and is not guarded —
 * callers are the PortAudio adapter and the concurrency tests, both of which
 * always pass a fully-initialised paTestData. */
void renderAudioBuffer(paTestData *data, float *out, unsigned long framesPerBuffer);

/* Master volume applied to the mixed output (0.0 .. 1.0). */
void audioSetMasterVolume(float v);
float audioGetMasterVolume(void);

/* --probe-tempo diagnostic (rule #1136: zero effect without the flag). Owned
 * by audio.c because the step counter is bumped in renderAudioBuffer, which
 * is where sequencer steps actually happen. */
extern bool g_probeTempo;
void probeTempoState(paTestData *data);

#endif /* SPECTRAX_AUDIO_H */