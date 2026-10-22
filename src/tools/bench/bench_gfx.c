/*
 * bench_gfx.c — minimal rendering loop that mimics a Spectrax instrument
 * screen's draw-call mix: textured quads (dial knobs), lines (route lines),
 * text (labels), rectangles (panel backgrounds). Captures FPS, frame ms, and
 * a draw-call approximation.
 *
 * Calibrated to be representative, not exhaustive — the full instrument
 * graph renderer is out of scope for the first bench pass.
 *
 * The window size comes from the device profile (or --display), so the
 * numbers describe the panel the device actually has.
 */
#include "bench_gfx.h"
#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double seconds_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int bench_gfx_run(const BenchOptions *opts, BenchReport *r) {
    int W = opts->width > 0 ? opts->width : 640;
    int H = opts->height > 0 ? opts->height : 480;
    int frames = opts->duration > 0 ? opts->duration * 60 : 600;

    SetTraceLogLevel(LOG_WARNING);
    InitWindow(W, H, "spectrax_bench[gfx]");
    SetTargetFPS(60);

    const char *cols[] = { "frame_idx", "frame_ms", "fps_inst" };
    bench_report_series_begin(r, "gfx_frames", cols, 3);

    char size_str[32];
    snprintf(size_str, sizeof size_str, "%dx%d", W, H);
    bench_report_str(r, "gfx", "window_size", size_str);

    double t0 = seconds_now();
    double min_ms = 1e9, max_ms = 0, sum_ms = 0;
    int frames_done = 0;

    for (int f = 0; f < frames; f++) {
        if (bench_stop_requested) break;
        double ft0 = seconds_now();
        BeginDrawing();
        ClearBackground((Color){ 24, 24, 28, 255 });

        /* Simulated panel grid: rectangles + lines. */
        for (int row = 0; row < 8; row++) {
            DrawRectangle(8, 40 + row * 50, W - 16, 44, (Color){ 32, 36, 44, 255 });
            DrawLine(8, 40 + row * 50, W - 8, 40 + row * 50, (Color){ 60, 60, 70, 255 });
        }

        /* Simulated dials: 16 circles + 16 lines (knob indicator). */
        for (int i = 0; i < 16; i++) {
            int x = 24 + (i % 8) * 78;
            int y = 60 + (i / 8) * 100;
            DrawCircle(x, y, 12, (Color){ 200, 200, 220, 255 });
            DrawLine(x, y, x + 10, y, RED);
        }

        /* Simulated labels: text per dial. */
        for (int i = 0; i < 16; i++) {
            char label[32];
            snprintf(label, sizeof label, "LABEL_%02d", i);
            DrawText(label, 24 + (i % 8) * 78, 80 + (i / 8) * 100, 9, WHITE);
        }

        DrawText("spectrax_bench gfx mode", 8, 8, 12, WHITE);
        DrawFPS(8, H - 20);
        EndDrawing();

        double ft_ms = (seconds_now() - ft0) * 1000.0;
        if (ft_ms < min_ms) min_ms = ft_ms;
        if (ft_ms > max_ms) max_ms = ft_ms;
        sum_ms += ft_ms;
        frames_done++;
        double row[3] = { (double)f, ft_ms, ft_ms > 0 ? 1000.0 / ft_ms : 0.0 };
        bench_report_series_row(r, "gfx_frames", row);
    }
    bench_report_series_end(r);

    double wall = seconds_now() - t0;
    if (frames_done > 0) {
        bench_report_metric(r, "gfx", "frames", frames_done);
        bench_report_metric(r, "gfx", "wallclock_s", wall);
        bench_report_metric(r, "gfx", "fps_avg", frames_done / wall);
        bench_report_metric(r, "gfx", "frame_ms_avg", sum_ms / frames_done);
        bench_report_metric(r, "gfx", "frame_ms_min", min_ms);
        bench_report_metric(r, "gfx", "frame_ms_max", max_ms);
        bench_report_metric(r, "gfx", "draw_calls_per_frame",
                            1 /*clear*/ + 8 /*bg rects*/ + 8 /*bg lines*/ +
                            16 /*circles*/ + 16 /*knob lines*/ + 16 /*labels*/ +
                            2 /*title+fps*/);
    }

    CloseWindow();
    return 0;
}
