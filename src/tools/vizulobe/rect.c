#include <string.h>
#include "rect.h"

static void rect_ensure(RectManager *rm, VizRect *vr, const char *path, int w, int h) {
	bool path_changed = vr->viz == NULL || strcmp(vr->viz->path, path) != 0;
	bool size_changed = !vr->rt_valid || vr->w != w || vr->h != h;
	if(path_changed) {
		if(vr->viz) {
			viz_free(vr->viz);
		}
		vr->viz = viz_load(path);
		vr->inited = false;
	}
	if(size_changed) {
		if(vr->rt_valid) {
			UnloadRenderTexture(vr->rt);
		}
		vr->rt = LoadRenderTexture(w, h);
		vr->w = w;
		vr->h = h;
		vr->rt_valid = true;
	}
}

static void rect_ensure_textures(RectManager *rm, const Analysis *a) {
	static float wave_rgba[VIZ_WAVEFORM_LEN * 4];
	static float spec_rgba[VIZ_SPECTRUM_MAX * 4];
	for(int i = 0; i < VIZ_WAVEFORM_LEN; i++) {
		wave_rgba[i * 4 + 0] = a->waveform[0][i];
		wave_rgba[i * 4 + 1] = a->waveform[1][i];
		wave_rgba[i * 4 + 2] = 0.0f;
		wave_rgba[i * 4 + 3] = 1.0f;
	}
	for(int i = 0; i < VIZ_SPECTRUM_MAX; i++) {
		spec_rgba[i * 4 + 0] = a->spectrum[0][i];
		spec_rgba[i * 4 + 1] = a->spectrum[1][i];
		spec_rgba[i * 4 + 2] = 0.0f;
		spec_rgba[i * 4 + 3] = 1.0f;
	}
	if(!rm->tex_valid) {
		Image wi = { 0 };
		wi.data = wave_rgba;
		wi.width = VIZ_WAVEFORM_LEN;
		wi.height = 1;
		wi.mipmaps = 1;
		wi.format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32;
		rm->waveform_tex = LoadTextureFromImage(wi);

		Image si = { 0 };
		si.data = spec_rgba;
		si.width = VIZ_SPECTRUM_MAX;
		si.height = 1;
		si.mipmaps = 1;
		si.format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32;
		rm->spectrum_tex = LoadTextureFromImage(si);
		rm->tex_valid = true;
	} else {
		UpdateTexture(rm->waveform_tex, wave_rgba);
		UpdateTexture(rm->spectrum_tex, spec_rgba);
	}
}

static void run_viz(VizRect *vr, Texture2D backbuffer, int w, int h,
	const Analysis *a, float time, float dt, RectManager *rm) {
	Viz *v = vr->viz;
	if(!v || v->kind == VIZ_KIND_ERR) {
		return;
	}
	if(v->kind == VIZ_KIND_GLSL) {
		BeginShaderMode(v->u.gl.shader);
		if(v->u.gl.loc_time >= 0) {
			SetShaderValue(v->u.gl.shader, v->u.gl.loc_time, &time, SHADER_UNIFORM_FLOAT);
		}
		if(v->u.gl.loc_dt >= 0) {
			SetShaderValue(v->u.gl.shader, v->u.gl.loc_dt, &dt, SHADER_UNIFORM_FLOAT);
		}
		Vector2 res = { (float)w, (float)h };
		if(v->u.gl.loc_resolution >= 0) {
			SetShaderValue(v->u.gl.shader, v->u.gl.loc_resolution, &res, SHADER_UNIFORM_VEC2);
		}
		Vector2 audio = { a->audio_l, a->audio_r };
		if(v->u.gl.loc_audio >= 0) {
			SetShaderValue(v->u.gl.shader, v->u.gl.loc_audio, &audio, SHADER_UNIFORM_VEC2);
		}
		if(v->u.gl.loc_waveform >= 0) {
			SetShaderValueTexture(v->u.gl.shader, v->u.gl.loc_waveform, rm->waveform_tex);
		}
		if(v->u.gl.loc_spectrum >= 0) {
			SetShaderValueTexture(v->u.gl.shader, v->u.gl.loc_spectrum, rm->spectrum_tex);
		}
		if(v->u.gl.loc_backbuffer >= 0) {
			SetShaderValueTexture(v->u.gl.shader, v->u.gl.loc_backbuffer, backbuffer);
		}
		DrawRectangle(0, 0, w, h, WHITE);
		EndShaderMode();
	} else if(v->kind == VIZ_KIND_C) {
		viz_t *ctx = &v->u.c.ctx;
		if(!vr->inited && v->u.c.init) {
			v->u.c.init(ctx);
			vr->inited = true;
		}
		ctx->time = time;
		ctx->dt = dt;
		memcpy(ctx->waveform, a->waveform, sizeof(ctx->waveform));
		memcpy(ctx->spectrum, a->spectrum, sizeof(ctx->spectrum));
		ctx->fft_bins = a->fft_bins;
		ctx->audio_l = a->audio_l;
		ctx->audio_r = a->audio_r;
		ctx->rect_w = w;
		ctx->rect_h = h;
		ctx->backbuffer = backbuffer;
		v->u.c.frame(ctx);
	}
}

static void render_rect(RectManager *rm, VizRect *vr, const Analysis *a,
	float time, float dt, int x, int y) {
	if(!vr->rt_valid || !vr->viz) {
		return;
	}
	if(vr->viz->kind == VIZ_KIND_ERR) {
		DrawRectangle(x, y, vr->w, vr->h, (Color){ 40, 0, 0, 255 });
		DrawText(vr->viz->error, x + 4, y + 4, 12, RED);
		return;
	}
	BeginTextureMode(vr->rt);
	ClearBackground(BLACK);
	DrawTexture(vr->rt.texture, 0, 0, WHITE);
	run_viz(vr, vr->rt.texture, vr->w, vr->h, a, time, dt, rm);
	EndTextureMode();
	DrawTextureRec(vr->rt.texture,
		(Rectangle){ 0, 0, (float)vr->w, -(float)vr->h },
		(Vector2){ (float)x, (float)y }, WHITE);
}

void rect_manager_init(RectManager *rm) {
	memset(rm, 0, sizeof(*rm));
}

void rect_manager_sync(RectManager *rm, const Scene *s, const Analysis *a) {
	for(int i = 0; i < s->fg_count; i++) {
		rect_ensure(rm, &rm->fg[i], s->fg[i].path, s->fg[i].w, s->fg[i].h);
	}
	if(s->bg_path[0]) {
		rect_ensure(rm, &rm->bg, s->bg_path, VIZ_SCREEN_W, VIZ_SCREEN_H);
	}
	rect_ensure_textures(rm, a);
}

void rect_manager_render(RectManager *rm, const Scene *s, const Analysis *a,
	float time, float dt) {
	if(s->bg_path[0]) {
		render_rect(rm, &rm->bg, a, time, dt, 0, 0);
	}
	for(int i = 0; i < s->fg_count; i++) {
		render_rect(rm, &rm->fg[i], a, time, dt, s->fg[i].x, s->fg[i].y);
	}
}

void rect_manager_shutdown(RectManager *rm) {
	for(int i = 0; i < VIZ_MAX_FG; i++) {
		if(rm->fg[i].viz) {
			viz_free(rm->fg[i].viz);
		}
		if(rm->fg[i].rt_valid) {
			UnloadRenderTexture(rm->fg[i].rt);
		}
	}
	if(rm->bg.viz) {
		viz_free(rm->bg.viz);
	}
	if(rm->bg.rt_valid) {
		UnloadRenderTexture(rm->bg.rt);
	}
	if(rm->tex_valid) {
		UnloadTexture(rm->waveform_tex);
		UnloadTexture(rm->spectrum_tex);
	}
	memset(rm, 0, sizeof(*rm));
}
