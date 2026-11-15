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

/* One preview panel per distinct voice type, in channel order: every channel
 * the song uses would be noise, and the question a panel answers is what a
 * sampler or a blep screen looks like next to an FM one. Channels at or past
 * `channelCount` (the enabled count) have no graph and are skipped. Returns
 * the number of channels written to `out`. */
int previewInstrumentChannels(const int *voiceTypes, int channelCount,
                              int *out, int max);

/* --- display modes -------------------------------------------------------
 *
 * The panel window has three ways to show the same set of panels, cycled with
 * TAB:
 *   ALL    every panel at once in a grid (the original view)
 *   PAGED  four panels per page, left/right for pages
 *   SINGLE one panel filling the window, left/right to step
 */
#define PREVIEW_PER_PAGE 4

typedef enum {
	PREVIEW_MODE_ALL = 0,
	PREVIEW_MODE_PAGED,
	PREVIEW_MODE_SINGLE,
	PREVIEW_MODE_COUNT
} PreviewMode;

PreviewMode previewNextMode(PreviewMode mode);
const char *previewModeName(PreviewMode mode);
PreviewMode previewModeFromName(const char *name);   /* unknown -> ALL */
int previewPageCount(int panels);
/* Wraps v into [0, n). n <= 0 gives 0. */
int previewWrapIndex(int v, int n);
/* Writes the panel indices visible in this mode and returns how many were
 * written. ALL: every panel; PAGED: the page's four; SINGLE: the one. */
int previewVisiblePanels(PreviewMode mode, int page, int index, int panels,
                         int *out, int max);

/* `--ui-preview-mode all|paged|single`, or NULL. Parsed with
 * previewModeFromName, which falls back to ALL for an unknown word. */
const char *uiPreviewModeArg(int argc, char **argv);

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

/* What the caller should do, derived from which files changed. */
#define PREVIEW_RELOAD_STYLE  0x1  /* recompile styles (theme or layout) */
#define PREVIEW_RELOAD_FONTS  0x2  /* reload the font atlas */
#define PREVIEW_RELOAD_LAYOUT 0x4  /* rebuild graphs (layout.json) */

/* Which file changed. previewWatchChanges reports these, so an acknowledged
 * reload stamps only the file it actually applied -- a shared "all three
 * are current" ack would swallow a theme edit that arrived while the layout
 * was being applied. */
#define PREVIEW_CHANGED_LAYOUT 0x1
#define PREVIEW_CHANGED_THEME  0x2
#define PREVIEW_CHANGED_FONT   0x4

/* Maps the changed-file bits to the reload actions above. */
int previewReloadMask(int changed);

/* Fills *w with the current mtimes on the first call (returning 0), and
 * afterwards returns the PREVIEW_RELOAD_* bits for the files that changed.
 * A NULL path is not watched.
 *
 * The stamps are NOT updated here: a reload that fails (a torn save, a JSON
 * error) must be retried, so the caller commits them with previewWatchAck
 * only after the new file has been applied. Without that, one bad edit wipes
 * the style tables for the rest of the run and is never retried. */
int previewWatchChanges(PreviewWatch *w, const char *layoutPath,
                        const char *themePath, const char *fontPath);

/* Takes the current mtimes of the files in `changed` (PREVIEW_CHANGED_*)
 * as applied. Call after a reload that succeeded for those files. */
void previewWatchAck(PreviewWatch *w, int changed, const char *layoutPath,
                     const char *themePath, const char *fontPath);

#endif