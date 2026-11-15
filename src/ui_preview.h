#ifndef UI_PREVIEW_H
#define UI_PREVIEW_H

#include <stdbool.h>
#include <time.h>

/* --ui-preview: the panel window.
 *
 * Renders the app's screens side by side into one window so a styling or
 * layout change can be judged across every screen at once, without
 * navigating to each on the handheld. No audio, no input, and nothing is
 * saved on exit: the simulator is an exporter, not a session.
 *
 * These helpers are pure (no raylib, no globals) and live in core_lib, not
 * in main.c: main.c is app-only and is recompiled into the harness with
 * -DSPECTRAX_HARNESS, so a helper there cannot be linked by tests/dsp. */

/* True when argv contains --ui-preview. */
bool uiPreviewRequested(int argc, char **argv);

/* The frame budget from `--ui-preview-frames N`, or 0 for "run until the
 * window is closed". A missing, negative or unparseable value is 0. */
int uiPreviewFrames(int argc, char **argv);

/* True when `path` exists and its mtime is newer than *stamp, and then
 * *stamp is updated. Sub-second resolution (st_mtim), because two saves in
 * the same second are a normal way to iterate on a layout file. */
bool fileNewerThan(const char *path, struct timespec *stamp);

#endif