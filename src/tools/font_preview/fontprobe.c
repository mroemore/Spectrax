/*
 * fontprobe.c - measure how a font actually rasterizes under raylib.
 *
 * Built against the vendored raylib (Spectrax ships libraylib.a + raylib.h),
 * this is ground truth for glyph metrics: raylib rasterizes TTF/OTF via
 * stb_truetype (which pads each glyph box and can clip oversized glyphs),
 * and PNG sprite fonts via LoadFontFromImage. fontTools math does NOT match
 * these numbers, so this probe is the primary measurement engine for the
 * font-ranking script.
 *
 * usage: fontprobe <path> [size]        (size ignored for .png)
 *
 * Emits one machine-parseable line on stdout:
 *   <path> | <ttf|png> | <size> | base=<n> | glyphs=<n> | maxW=<n> | maxH=<n> | maxAdv=<n> | offX=<n> | offY=<n>
 * or, on load failure:
 *   <path> | <ttf|png> | <size> | FAIL
 */
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: fontprobe <path> [size]\n"); return 1; }
    const char *path = argv[1];
    int size = (argc > 2) ? atoi(argv[2]) : 10;
    const char *ext = strrchr(path, '.');
    int kind = (ext && (strcmp(ext, ".png") == 0)) ? 1 : 0;

    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(64, 64, "probe");

    Font f = { 0 };
    if (kind) f = LoadFont(path);
    else      f = LoadFontEx(path, size, NULL, 255);

    if (f.texture.id == 0 || f.glyphCount == 0) {
        printf("%s | %s | %d | FAIL\n", path, kind ? "png" : "ttf", size);
        CloseWindow();
        return 0;
    }

    float maxAdv = 0.0f, maxW = 0.0f, maxH = 0.0f, maxOffX = 0.0f, maxOffY = 0.0f;
    for (int i = 0; i < f.glyphCount; i++) {
        GlyphInfo g = f.glyphs[i];
        if (g.advanceX > maxAdv) maxAdv = g.advanceX;
        if (g.image.width > maxW) maxW = g.image.width;
        if (g.image.height > maxH) maxH = g.image.height;
        if (g.offsetX > maxOffX) maxOffX = g.offsetX;
        if (g.offsetY > maxOffY) maxOffY = g.offsetY;
    }
    printf("%s | %s | %d | base=%d | glyphs=%d | maxW=%.0f | maxH=%.0f | maxAdv=%.0f | offX=%.0f | offY=%.0f\n",
           path, kind ? "png" : "ttf", size, f.baseSize, f.glyphCount, maxW, maxH, maxAdv, maxOffX, maxOffY);

    if (!kind) UnloadFont(f);
    CloseWindow();
    return 0;
}