/*
 * preview_fonts.c - side-by-side pixel-font comparison for Spectrax.
 *
 * Renders a grid: rows are sample strings lifted from Spectrax's UI source,
 * columns are fonts. By default the columns are the fonts Spectrax currently
 * uses (pixelFont = console.ttf @9, textFont = setback.png @12) plus the top
 * candidates from rank_fonts.py's profile ranking, drawn at the size the
 * ranking chose for each. With --all it auto-discovers and renders EVERY
 * font file in the Spectrax tree.
 *
 * Build (against Spectrax's vendored raylib):
 *   gcc preview_fonts.c -I<spectrax>/include -L<spectrax>/lib/linux \
 *       -lraylib -lm -o preview_fonts
 *
 * Usage:
 *   preview_fonts [root]                  interactive (scroll/pan), default root ~/proj/Spectrax/bin
 *   preview_fonts --screenshot OUT [root] curated grid -> OUT.png, exit
 *   preview_fonts --all OUT [root] [--size N]  every font -> OUT.png, exit
 *
 * Export modes are headless: if no DISPLAY is set, a private Xvfb on :97 is
 * started automatically and torn down afterwards. PNG sprite fonts render at
 * their native cell; TTF fonts render at the size the ranking chose (curated)
 * or --size N (default 10) in --all mode.
 *
 * Controls (interactive):
 *   mouse wheel          vertical scroll
 *   shift + mouse wheel  horizontal scroll
 *   left drag            pan
 */
#include "raylib.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define PAD_Y 20
#define HEADER_H 34
#define TITLE_H 26

typedef struct {
    char path[2048];          /* absolute path */
    char label[128];
    int size;                 /* render size (0 => PNG native) */
    int isRef;                /* current font vs candidate */
    Font font;
    int columnW;
} FontEntry;

static FontEntry *fonts = NULL;
static int fontCount = 0;
static int fontCap = 0;

/* Sample strings pulled from Spectrax's DrawText/DrawTextEx calls. */
static const char *samples[] = {
    "SPECTRAX",
    "VLINE",
    "POLY",
    "LFO",
    "BPM",
    "WAVE",
    "ATTACK",
    "ENVELOPE",
    "SAMPLE",
    "HAMMING",
    "SHAPE",
    "BLACKMAN ESTIMATED",
    "OVERWRITE",
    "(CLOSED)",
    "Bass LFO (0.1 Hz)",
    "Bass Rate: 0.23",
    "Fast Square (2.0 Hz + Mid/Ultra mod)",
};
static const int sampleCount = (int)(sizeof(samples) / sizeof(samples[0]));

static Font uiFont;   /* raylib default font, for headers / row labels */

static char gridTitle[128] = "Spectrax pixel-font comparison: current vs top-5 candidates";

static char rootPath[1024] = "~/proj/Spectrax";

/* Resolve a path relative to the Spectrax root; also accepts the bin/
 * working-dir form (resources/... lives under bin/). */
static void resolvePath(char *out, size_t n, const char *rel)
{
    snprintf(out, n, "%s/%s", rootPath, rel);
    if (access(out, F_OK) != 0) {
        char alt[2300];
        snprintf(alt, sizeof(alt), "%s/bin/%s", rootPath, rel);
        if (access(alt, F_OK) == 0) snprintf(out, n, "%s", alt);
    }
}

static void addFont(const char *path, const char *label, int size, int isRef)
{
    if (fontCount >= fontCap) {
        fontCap = fontCap ? fontCap * 2 : 16;
        fonts = realloc(fonts, fontCap * sizeof(FontEntry));
    }
    FontEntry *e = &fonts[fontCount++];
    snprintf(e->path, sizeof(e->path), "%s", path);
    snprintf(e->label, sizeof(e->label), "%s", label);
    e->size = size;
    e->isRef = isRef;
    e->font = (Font){ 0 };
    e->columnW = 0;
}

static int endsWith(const char *s, const char *suffix)
{
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

static const char *baseName(const char *p)
{
    const char *b = strrchr(p, '/');
    return b ? b + 1 : p;
}

/* Sort: current fonts (refs) first, then the rest by filename. */
static int cmpFonts(const void *a, const void *b)
{
    const FontEntry *fa = a, *fb = b;
    if (fa->isRef != fb->isRef) return fb->isRef - fa->isRef;
    return strcasecmp(baseName(fa->path), baseName(fb->path));
}

static void sortFonts(void)
{
    qsort(fonts, fontCount, sizeof(FontEntry), cmpFonts);
}

/* Recursively collect font files: *.ttf/*.TTF/*.otf anywhere; *.png under
 * directories named "fonts" or "font". */
static void collectFonts(const char *dir, int ttfSize, int isCuratedRoot)
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char full[2300];
        snprintf(full, sizeof(full), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            collectFonts(full, ttfSize, 0);
        } else if (S_ISREG(st.st_mode)) {
            const char *base = ent->d_name;
            const char *dirBase = strrchr(dir, '/');
            dirBase = dirBase ? dirBase + 1 : dir;
            int inFontDir = (strcmp(dirBase, "fonts") == 0 || strcmp(dirBase, "font") == 0);
            int isTtf = endsWith(base, ".ttf") || endsWith(base, ".TTF") || endsWith(base, ".otf") || endsWith(base, ".OTF");
            int isPng = inFontDir && endsWith(base, ".png");
            if (!isTtf && !isPng) continue;
            int isRef = (strcmp(base, "console.ttf") == 0) || (strcmp(base, "setback.png") == 0);
            int size = isTtf ? ttfSize : 0;
            char label[128];
            if (isTtf) snprintf(label, sizeof(label), "%s @%d", base, ttfSize);
            else snprintf(label, sizeof(label), "%s", base);
            addFont(full, label, size, isRef);
        }
    }
    closedir(d);
}

static void loadFonts(void)
{
    for (int i = 0; i < fontCount; i++) {
        FontEntry *e = &fonts[i];
        if (endsWith(e->path, ".png")) {
            e->font = LoadFont(e->path);
        } else {
            int size = e->size > 0 ? e->size : 10;
            e->font = LoadFontEx(e->path, size, NULL, 255);
        }
        if (e->font.glyphCount == 0 || e->font.texture.id == 0) {
            printf("WARNING: failed to load %s\n", e->path);
        } else {
            SetTextureFilter(e->font.texture, TEXTURE_FILTER_POINT);
            printf("loaded %s (base %d, %d glyphs)\n", e->label, e->font.baseSize, e->font.glyphCount);
        }
    }
}

/* Measure the grid; returns the full content size. */
static Vector2 measureGrid(int *colW, int *rowH)
{
    colW[0] = 190;
    int maxRowH = 0;
    for (int i = 0; i < sampleCount; i++) {
        Vector2 m = MeasureTextEx(uiFont, samples[i], 10, 1);
        if ((int)m.x + 12 > colW[0]) colW[0] = (int)m.x + 12;
    }
    for (int c = 0; c < fontCount; c++) {
        Vector2 lm = MeasureTextEx(uiFont, fonts[c].label, 10, 1);
        int w = (int)lm.x + 16;
        int h = fonts[c].font.baseSize > 0 ? fonts[c].font.baseSize : fonts[c].size;
        if (h > maxRowH) maxRowH = h;
        for (int i = 0; i < sampleCount; i++) {
            Vector2 m = MeasureTextEx(fonts[c].font, samples[i], fonts[c].font.baseSize, 1);
            if ((int)m.x + 8 > w) w = (int)m.x + 8;
        }
        fonts[c].columnW = w;
        colW[c + 1] = w;
    }
    if (maxRowH < 16) maxRowH = 16;
    *rowH = maxRowH + PAD_Y;

    int totalW = colW[0];
    for (int c = 0; c < fontCount; c++) totalW += colW[c + 1];
    int totalH = TITLE_H + HEADER_H + sampleCount * (*rowH) + PAD_Y;
    return (Vector2){ (float)totalW, (float)totalH };
}

/* Draw the whole grid (headers + rows + title + footer) into the currently
 * active render target. Caller wraps this in BeginDrawing/EndDrawing or
 * BeginTextureMode/EndTextureMode. targetW/H are the active target's size. */
static void drawContent(int targetW, int targetH, float scrollX, float scrollY,
                        int *colW, int rowH)
{
    for (int c = 0; c < fontCount; c++) {
        int x = colW[0] - (int)scrollX;
        for (int k = 0; k < c; k++) x += colW[k + 1];
        /* header cell */
        Color hc = fonts[c].isRef ? (Color){ 90, 90, 110, 255 } : (Color){ 40, 80, 45, 255 };
        DrawRectangle(x, TITLE_H, colW[c + 1], HEADER_H, hc);
        DrawTextEx(uiFont, fonts[c].label, (Vector2){ x + 4, TITLE_H + 12 }, 10, 1, WHITE);
        /* vertical separator */
        DrawLine(x, TITLE_H, x, targetH, (Color){ 40, 40, 48, 255 });
    }
    /* row labels (sample strings, neutral font) */
    for (int r = 0; r < sampleCount; r++) {
        int y = TITLE_H + HEADER_H + r * rowH - (int)scrollY;
        if (y + rowH < 0 || y > targetH) continue;
        DrawRectangle(0, y, colW[0], rowH, (Color){ 30, 30, 36, 255 });
        DrawTextEx(uiFont, samples[r], (Vector2){ 6, y + (rowH - 12) / 2 }, 10, 1, (Color){ 150, 150, 160, 255 });
        DrawLine(0, y, targetW, y, (Color){ 40, 40, 48, 255 });
    }
    /* text cells */
    for (int c = 0; c < fontCount; c++) {
        int x = colW[0] - (int)scrollX;
        for (int k = 0; k < c; k++) x += colW[k + 1];
        for (int r = 0; r < sampleCount; r++) {
            int y = TITLE_H + HEADER_H + r * rowH - (int)scrollY;
            if (y + rowH < 0 || y > targetH) continue;
            Font f = fonts[c].font;
            if (f.glyphCount == 0) continue;
            Vector2 m = MeasureTextEx(f, samples[r], f.baseSize, 1);
            Vector2 pos = { x + (fonts[c].columnW - m.x) / 2,
                            y + (rowH - f.baseSize) / 2 + 1 };
            DrawTextEx(f, samples[r], pos, f.baseSize, 1,
                       fonts[c].isRef ? (Color){ 255, 214, 140, 255 } : WHITE);
        }
    }
    /* title + footer */
    DrawTextEx(uiFont, gridTitle,
               (Vector2){ 6, 7 }, 12, 1, (Color){ 200, 200, 210, 255 });
    DrawTextEx(uiFont, "wheel=vertical  shift+wheel=horizontal  drag=pan  esc=quit",
               (Vector2){ 6, targetH - 18 }, 10, 1, (Color){ 120, 120, 130, 255 });
}

static void drawFrame(float scrollX, float scrollY, int *colW, int rowH)
{
    BeginDrawing();
    ClearBackground((Color){ 22, 22, 26, 255 });
    drawContent(GetScreenWidth(), GetScreenHeight(), scrollX, scrollY, colW, rowH);
    EndDrawing();
}

/* Export the full grid to OUT.png. Uses an offscreen RenderTexture so the
 * output is independent of the window size. */
static void exportGrid(const char *out, int *colW, int rowH, Vector2 content)
{
    RenderTexture2D rt = LoadRenderTexture((int)content.x, (int)content.y);
    BeginTextureMode(rt);
    ClearBackground((Color){ 22, 22, 26, 255 });
    drawContent(rt.texture.width, rt.texture.height, 0, 0, colW, rowH);
    EndTextureMode();
    Image img = LoadImageFromTexture(rt.texture);
    ImageFlipVertical(&img);   /* render textures are stored upside down */
    ExportImage(img, out);
    UnloadImage(img);
    UnloadRenderTexture(rt);
    printf("wrote %s (%dx%d, %d fonts)\n", out, (int)content.x, (int)content.y, fontCount);
}

/* Headless support: if no DISPLAY is available, start a private Xvfb on :97. */
static int spawnedXvfb = 0;
static void ensureDisplay(void)
{
    if (getenv("DISPLAY")) return;
    if (system("command -v Xvfb >/dev/null 2>&1") != 0) return;
    system("Xvfb :97 -screen 0 2600x1800x24 >/dev/null 2>&1 &");
    setenv("DISPLAY", ":97", 1);
    spawnedXvfb = 1;
    sleep(1);
}

static void teardownDisplay(void)
{
    if (spawnedXvfb) system("pkill -f 'Xvfb :97' >/dev/null 2>&1");
}

/* Build the curated top-5 candidate list (default interactive view). */
static void buildCurated(void)
{
    static const struct {
        const char *rel;
        const char *label;
        int size;
        int isRef;
    } curated[] = {
        { "resources/fonts/console.ttf",                    "current pixelFont: console.ttf",  9,  1 },
        { "resources/fonts/setback.png",                    "current textFont: setback.png",  12,  1 },
        { "resources/fonts/Daydream.ttf",                   "#1 Daydream.ttf",                 9,  0 },
        { "resources/fonts/DigitalDisco.ttf",               "#2 DigitalDisco.ttf",            10,  0 },
        { "resources/fonts/DigitalDisco-Thin.ttf",          "#3 DigitalDisco-Thin.ttf",       10,  0 },
        { "../src/tools/sample_analyser/themevck-text.ttf", "#3 themevck-text.ttf",           10,  0 },
        { "resources/fonts/KiwiSoda.ttf",                   "#3 KiwiSoda.ttf",                 9,  0 },
    };
    for (unsigned i = 0; i < sizeof(curated) / sizeof(curated[0]); i++) {
        char path[2048];
        resolvePath(path, sizeof(path), curated[i].rel);
        addFont(path, curated[i].label, curated[i].size, curated[i].isRef);
    }
}

int main(int argc, char **argv)
{
    const char *shot = NULL;
    const char *out = NULL;
    int allMode = 0;
    int ttfSize = 10;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) shot = argv[++i];
        else if (strcmp(argv[i], "--all") == 0 && i + 1 < argc) { allMode = 1; out = argv[++i]; }
        else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) ttfSize = atoi(argv[++i]);
        else snprintf(rootPath, sizeof(rootPath), "%s", argv[i]);
    }
    if (rootPath[0] == '~') {
        const char *home = getenv("HOME");
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "%s%s", home ? home : "", rootPath + 1);
        snprintf(rootPath, sizeof(rootPath), "%s", tmp);
    }

    if (shot || out) ensureDisplay();

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 800, "Spectrax font preview");
    SetTargetFPS(60);
    uiFont = GetFontDefault();

    if (allMode) {
        collectFonts(rootPath, ttfSize, 1);
        sortFonts();
        snprintf(gridTitle, sizeof(gridTitle), "Spectrax pixel-font comparison: all %d fonts", fontCount);
    } else {
        buildCurated();
    }
    if (fontCount == 0) {
        printf("no fonts found under %s\n", rootPath);
        teardownDisplay();
        return 1;
    }
    loadFonts();

    int colW[64 + 1] = { 0 };
    int rowH = 0;
    Vector2 content = measureGrid(colW, &rowH);

    if (out || shot) {
        exportGrid(out ? out : shot, colW, rowH, content);
        for (int i = 0; i < fontCount; i++) UnloadFont(fonts[i].font);
        CloseWindow();
        teardownDisplay();
        return 0;
    }

    float scrollX = 0.0f, scrollY = 0.0f;
    bool dragging = false;
    Vector2 dragStart = { 0 };

    while (!WindowShouldClose()) {
        float wheel = GetMouseWheelMove();
        if (IsKeyDown(KEY_LEFT_SHIFT)) {
            scrollX -= wheel * 40.0f;
        } else {
            scrollY -= wheel * 40.0f;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            dragging = true;
            dragStart = GetMousePosition();
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) dragging = false;
        if (dragging) {
            Vector2 m = GetMousePosition();
            scrollX -= (m.x - dragStart.x);
            scrollY -= (m.y - dragStart.y);
            dragStart = m;
        }
        float maxX = content.x - GetScreenWidth();
        float maxY = content.y - GetScreenHeight();
        if (scrollX < 0) scrollX = 0;
        if (scrollY < 0) scrollY = 0;
        if (scrollX > maxX) scrollX = maxX;
        if (scrollY > maxY) scrollY = maxY;

        drawFrame(scrollX, scrollY, colW, rowH);
    }

    for (int i = 0; i < fontCount; i++) UnloadFont(fonts[i].font);
    CloseWindow();
    return 0;
}