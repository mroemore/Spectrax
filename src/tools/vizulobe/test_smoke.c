#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raylib.h"
#include "audio.h"
#include "analysis.h"
#include "scene.h"
#include "viz.h"
#include "rect.h"

static bool run_script(const char *script_path, const char *device, int fft_bins) {
	FILE *f = fopen(script_path, "r");
	if(!f) {
		fprintf(stderr, "smoke: cannot open %s\n", script_path);
		return false;
	}

	Analysis analysis;
	analysis_init(&analysis, fft_bins);
	Scene scene;
	scene_init(&scene);
	RectManager rm;
	rect_manager_init(&rm);

	AudioCapture audio;
	if(audio_init(&audio, device) != 0) {
		/* silent */
	}

	float last = (float)GetTime();
	char line[1024];
	while(fgets(line, sizeof(line), f)) {
		char cmd[64];
		char arg[768];
		if(sscanf(line, "%63s %767[^\n]", cmd, arg) < 1) {
			continue;
		}
		if(strcmp(cmd, "LOADL") == 0) {
			scene_add_fg(&scene, arg, 100, 100, 320, 240);
		} else if(strcmp(cmd, "LOADB") == 0) {
			scene_set_bg(&scene, arg);
		} else if(strcmp(cmd, "WAIT") == 0) {
			int frames = atoi(arg);
			for(int i = 0; i < frames; i++) {
				float now = (float)GetTime();
				float dt = now - last;
				last = now;
				float bl[VIZ_FRAMES_PER_BLOCK], br[VIZ_FRAMES_PER_BLOCK];
				int got = audio_drain(&audio, bl, br, VIZ_FRAMES_PER_BLOCK);
				for(int j = 0; j < got; j++) {
					analysis_push(&analysis, bl[j], br[j]);
				}
				if(got > 0) {
					analysis_block_done(&analysis);
				}
				rect_manager_sync(&rm, &scene, &analysis);
				BeginDrawing();
				ClearBackground(BLACK);
				rect_manager_render(&rm, &scene, &analysis, now, dt);
				EndDrawing();
			}
		} else if(strcmp(cmd, "SHOT") == 0) {
			TakeScreenshot(arg);
		} else if(strcmp(cmd, "QUIT") == 0) {
			break;
		}
	}
	fclose(f);

	rect_manager_shutdown(&rm);
	audio_shutdown(&audio);
	return true;
}

int main(int argc, char **argv) {
	const char *script = NULL;
	const char *device = NULL;
	int fft_bins = 512;
	for(int i = 1; i < argc; i++) {
		if(strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
			script = argv[++i];
		} else if(strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
			device = argv[++i];
		} else if(strcmp(argv[i], "--fft") == 0 && i + 1 < argc) {
			fft_bins = atoi(argv[++i]);
		}
	}
	if(!script) {
		fprintf(stderr, "usage: vizulobe --script file.txt [-d device] [--fft bins]\n");
		return 1;
	}

	SetTraceLogLevel(LOG_WARNING);
	InitWindow(VIZ_SCREEN_W, VIZ_SCREEN_H, "vizulobe smoke");
	SetTargetFPS(60);
	bool ok = run_script(script, device, fft_bins);
	CloseWindow();
	return ok ? 0 : 1;
}
