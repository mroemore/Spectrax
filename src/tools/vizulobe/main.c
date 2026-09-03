#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"
#include "audio.h"
#include "analysis.h"
#include "scene.h"
#include "viz.h"
#include "project.h"
#include "rect.h"

typedef struct {
	char text[512];
	int cursor;
	bool active;
	char title[64];
} TextPrompt;

static void prompt_begin(TextPrompt *p, const char *title) {
	p->active = true;
	p->cursor = 0;
	p->text[0] = '\0';
	snprintf(p->title, sizeof(p->title), "%s", title);
}

static void prompt_update(TextPrompt *p) {
	int ch = GetCharPressed();
	while(ch > 0) {
		if(ch >= 32 && ch < 127 && p->cursor < (int)sizeof(p->text) - 1) {
			p->text[p->cursor++] = (char)ch;
			p->text[p->cursor] = '\0';
		}
		ch = GetCharPressed();
	}
	if(IsKeyPressed(KEY_BACKSPACE) && p->cursor > 0) {
		p->text[--p->cursor] = '\0';
	}
}

static void prompt_draw(const TextPrompt *p) {
	DrawRectangle(10, 10, 400, 40, (Color){ 0, 0, 0, 220 });
	DrawText(p->title, 14, 16, 16, GRAY);
	DrawText(p->text, 14 + MeasureText(p->title, 16) + 8, 16, 16, WHITE);
}

static void parse_args(int argc, char **argv, const char **device,
	int *fft_bins, const char **project, bool *list_devices) {
	*device = NULL;
	*fft_bins = 512;
	*project = NULL;
	*list_devices = false;
	for(int i = 1; i < argc; i++) {
		if(strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--device") == 0) {
			if(i + 1 < argc) {
				*device = argv[++i];
			}
		} else if(strcmp(argv[i], "--fft") == 0) {
			if(i + 1 < argc) {
				*fft_bins = atoi(argv[++i]);
			}
		} else if(strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--project") == 0) {
			if(i + 1 < argc) {
				*project = argv[++i];
			}
		} else if(strcmp(argv[i], "--list-devices") == 0) {
			*list_devices = true;
		}
	}
}

int main(int argc, char **argv) {
	const char *device = NULL;
	int fft_bins = 512;
	const char *project = NULL;
	bool list_devices = false;
	parse_args(argc, argv, &device, &fft_bins, &project, &list_devices);

	if(fft_bins < 16) {
		fft_bins = 16;
	}
	if(fft_bins > 512) {
		fft_bins = 512;
	}

	if(list_devices) {
		audio_list_devices();
		return 0;
	}

	AudioCapture audio;
	if(audio_init(&audio, device) != 0) {
		/* non-fatal: run silent */
	}

	SetTraceLogLevel(LOG_WARNING);
	InitWindow(VIZ_SCREEN_W, VIZ_SCREEN_H, "vizulobe");
	SetTargetFPS(60);

	Analysis analysis;
	analysis_init(&analysis, fft_bins);

	Scene scene;
	scene_init(&scene);

	RectManager rm;
	rect_manager_init(&rm);

	if(project) {
		ProjectFile pf;
		if(project_load(project, &pf) == 0) {
			scene_set_bg(&scene, pf.bg_path);
			for(int i = 0; i < pf.fg_count; i++) {
				scene_add_fg(&scene, pf.fg[i].path, pf.fg[i].x, pf.fg[i].y, pf.fg[i].w, pf.fg[i].h);
			}
		} else {
			fprintf(stderr, "[vizulobe] failed to load project '%s'\n", project);
		}
	}

	TextPrompt prompt = {0};
	int prompt_mode = 0; /* 0 none, 1 load fg, 2 load bg, 3 save project, 4 load project */
	char project_path[512] = "project.json";

	bool dragging = false;
	bool resizing = false;
	int drag_start_x = 0, drag_start_y = 0;
	float last_time = (float)GetTime();
	bool show_helper = true;

	while(!WindowShouldClose()) {
		float now = (float)GetTime();
		float dt = now - last_time;
		last_time = now;

		/* audio -> analysis */
		float bl[VIZ_FRAMES_PER_BLOCK], br[VIZ_FRAMES_PER_BLOCK];
		int got = audio_drain(&audio, bl, br, VIZ_FRAMES_PER_BLOCK);
		for(int i = 0; i < got; i++) {
			analysis_push(&analysis, bl[i], br[i]);
		}
		if(got > 0) {
			analysis_block_done(&analysis);
		}

		/* helper box: dismiss on first click */
		if(show_helper && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
			show_helper = false;
		}

		/* prompt handling */
		if(prompt.active) {
			prompt_update(&prompt);
			if(IsKeyPressed(KEY_ENTER)) {
				const char *path = prompt.text;
				if(prompt_mode == 1 || prompt_mode == 2) {
					Vector2 m = GetMousePosition();
					if(prompt_mode == 1) {
						int idx = scene_add_fg(&scene, path, (int)m.x - 160, (int)m.y - 120, 320, 240);
						if(idx < 0) {
							fprintf(stderr, "[vizulobe] fg full (max %d)\n", VIZ_MAX_FG);
						}
					} else {
						scene_set_bg(&scene, path);
					}
				} else if(prompt_mode == 3) {
					snprintf(project_path, sizeof(project_path), "%s", path);
					if(project_save(project_path, &scene, analysis.fft_bins) != 0) {
						fprintf(stderr, "[vizulobe] save failed: %s\n", project_path);
					}
				} else if(prompt_mode == 4) {
					ProjectFile pf;
					if(project_load(path, &pf) == 0) {
						scene_init(&scene);
						scene_set_bg(&scene, pf.bg_path);
						for(int i = 0; i < pf.fg_count; i++) {
							scene_add_fg(&scene, pf.fg[i].path, pf.fg[i].x, pf.fg[i].y, pf.fg[i].w, pf.fg[i].h);
						}
					} else {
						fprintf(stderr, "[vizulobe] load failed: %s\n", path);
					}
				}
				prompt.active = false;
			}
			if(IsKeyPressed(KEY_ESCAPE)) {
				prompt.active = false;
			}
		} else {
			/* keybinds: gated off ctrl so ctrl+drag is free for resize */
			bool ctrl_held = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
			if(!ctrl_held) {
				if(IsKeyPressed(KEY_L)) {
					prompt_begin(&prompt, "load fg viz:");
					prompt_mode = 1;
				} else if(IsKeyPressed(KEY_B)) {
					prompt_begin(&prompt, "load bg viz:");
					prompt_mode = 2;
				} else if(IsKeyPressed(KEY_S)) {
					prompt_begin(&prompt, "save project:");
					snprintf(prompt.text, sizeof(prompt.text), "%s", project_path);
					prompt.cursor = (int)strlen(prompt.text);
					prompt_mode = 3;
				} else if(IsKeyPressed(KEY_R)) {
					prompt_begin(&prompt, "load project:");
					prompt_mode = 4;
				} else if(IsKeyPressed(KEY_DELETE)) {
					scene_remove_fg(&scene, scene.selected);
				}
			}

			/* mouse: click selects, drag moves, ctrl+drag resizes */
			if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
				int hit = scene_hit_test(&scene, (int)GetMouseX(), (int)GetMouseY());
				scene_select(&scene, hit);
				drag_start_x = GetMouseX();
				drag_start_y = GetMouseY();
				if(hit >= 0) {
					dragging = !ctrl_held;
					resizing = ctrl_held;
				}
			}
			if(dragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
				scene_drag(&scene, GetMouseX() - drag_start_x, GetMouseY() - drag_start_y);
				drag_start_x = GetMouseX();
				drag_start_y = GetMouseY();
			}
			if(resizing && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
				scene_resize(&scene, GetMouseX() - drag_start_x, GetMouseY() - drag_start_y);
				drag_start_x = GetMouseX();
				drag_start_y = GetMouseY();
			}
			if(IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
				dragging = false;
				resizing = false;
			}
		}

		rect_manager_sync(&rm, &scene, &analysis);

		BeginDrawing();
		ClearBackground(BLACK);
		rect_manager_render(&rm, &scene, &analysis, now, dt);

		/* selection outline */
		if(scene.selected >= 0 && scene.selected < scene.fg_count) {
			const FgVizRect *r = &scene.fg[scene.selected];
			DrawRectangleLines(r->x, r->y, r->w, r->h, WHITE);
		}

		if(show_helper) {
			DrawRectangle(10, 10, 620, 86, (Color){ 0, 0, 0, 200 });
			DrawText("L load fg viz   B load bg viz   S save   R load   Del remove", 16, 14, 16, WHITE);
			DrawText("drag move   ctrl+drag resize   click select", 16, 34, 16, GRAY);
			DrawText("CLI: -d device  --fft bins  -p project  --list-devices", 16, 54, 16, GRAY);
			DrawText("(click to dismiss)", 16, 72, 14, DARKGRAY);
		}

		if(prompt.active) {
			prompt_draw(&prompt);
		}

		DrawFPS(VIZ_SCREEN_W - 70, 8);
		EndDrawing();
	}

	/* teardown: GL resources BEFORE CloseWindow, audio after */
	rect_manager_shutdown(&rm);
	CloseWindow();
	audio_shutdown(&audio);

	printf("bye\n");
	return 0;
}