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

/* The path prefix from `--ui-preview-shot BASE`, or NULL. Each panel is
 * written to BASE-<name>.png after the frame budget is spent, read back from
 * its own target: TakeScreenshot is blank under llvmpipe, and the render
 * texture is already exactly one screen. */
const char *uiPreviewShotPrefix(int argc, char **argv);

/* --- hot reload -----------------------------------------------------------
 *
 * The preview watches three files and reports what changed since the last
 * call. The split matters because the three reload along different paths:
 * the theme and the layout's style section recompile the style tables in
 * place, the layout's `layouts` section is baked into the node coordinates
 * by applyLayout and needs the graphs rebuilt, and a font file needs the
 * atlas reloaded (there is no UnloadFont call anywhere else in the app).
 */
typedef struct {
	struct timespec layout;
	struct timespec theme;
	struct timespec font;
	int primed;
} PreviewWatch;

#define PREVIEW_RELOAD_STYLE  0x1  /* recompile styles (theme or layout) */
#define PREVIEW_RELOAD_FONTS  0x2  /* reload the font atlas */
#define PREVIEW_RELOAD_LAYOUT 0x4  /* rebuild graphs (layout.json) */

/* Fills *w with the current mtimes on the first call (returning 0), and
 * afterwards returns the PREVIEW_RELOAD_* bits for the files that changed.
 * A NULL path is not watched. */
int previewWatchChanges(PreviewWatch *w, const char *layoutPath,
                        const char *themePath, const char *fontPath);

#endif