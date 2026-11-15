#ifndef UI_PREVIEW_MOCK_H
#define UI_PREVIEW_MOCK_H

#include <stdbool.h>

#include "main.h"       /* paTestData (the app's whole data set) */
#include "appstate.h"   /* ApplicationState */

/* Mock data for the --ui-preview panel window.
 *
 * The preview renders screens that are normally alive: dials with modulation
 * segments, a playhead walking the song, a scrolling waveform, an FFT. With
 * no audio and no input they would all be still, so this module produces
 * deterministic synthetic data for them.
 *
 * Everything here is a function of the frame counter alone -- no wall clock,
 * no rand() -- so two runs at the same frame count produce identical panels,
 * which is what makes a shot comparison meaningful. No libm: the preview loop
 * runs on the same 60 fps budget as the app's maths, which never calls
 * sinf/cosf.
 */

/* Frames each demo subject is shown before the next one is selected: 1.5 s
 * at 60 fps. The smoke's frame budgets are multiples of this. */
#define PREVIEW_MOCK_HOLD_FRAMES 90

/* The active subject for this frame, in 0..subjects-1. subjects <= 0 or
 * holdFrames <= 0 gives 0. */
int previewMockSubject(int frame, int subjects, int holdFrames);

/* A triangle wave in [-amplitude, amplitude], period in frames, phase in
 * 0..255 (128 starts at zero, falling). period <= 0 returns 0. */
int previewMockTriangle(int frame, int period, int amplitude, int phase);

/* One synthetic audio sample in [-1, 1] for the visualisers. */
float previewMockSample(int frame);

/* The playhead row for a run of `rows` rows, advancing one row every
 * framesPerRow frames and wrapping. rows <= 0 or framesPerRow <= 0 gives 0. */
int previewMockPlayhead(int frame, int rows, int framesPerRow);

/* value forced into [minValue, maxValue]; an inverted range returns minValue. */
float previewMockClampToRange(float value, float minValue, float maxValue);

/* --- the driver ----------------------------------------------------------
 *
 * Called only from runUiPreview. Both entry points are no-ops when the
 * instrument the preview shows has no parameter list. */

/* Enables the visualisers, creates the demo modulators and routes them, and
 * parks the playheads at row 0. */
void previewMockInit(paTestData *data, ApplicationState *appState);

/* One frame of mock time: advance the modulation engine by a fixed 1/60 s,
 * push synthetic samples into the FFT, the time graph and the buffer
 * scroller, move the playheads, and re-route the modulators when the subject
 * timer ticks over. */
void previewMockStep(paTestData *data, ApplicationState *appState, int frame);

#endif