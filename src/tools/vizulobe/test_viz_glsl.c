#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "raylib.h"
#include "viz.h"

int main(void) {
	mkdir(".tmp_files", 0755);
	InitWindow(640, 480, "vizulobe glsl test");
	SetTraceLogLevel(LOG_NONE);

	FILE *f = fopen(".tmp_files/good.frag", "w");
	fprintf(f,
		"#version 330\n"
		"uniform float uTime;\n"
		"uniform vec2 uResolution;\n"
		"uniform vec2 uAudio;\n"
		"uniform sampler2D uWaveform;\n"
		"uniform sampler2D uSpectrum;\n"
		"uniform sampler2D uBackbuffer;\n"
		"out vec4 FragColor;\n"
		"void main() {\n"
		"  vec2 uv = gl_FragCoord.xy / uResolution;\n"
		"  vec2 shift = uAudio * sin(uTime);\n"
		"  float wave = texture(uWaveform, vec2(uv.x + shift.x, 0.0)).r;\n"
		"  float spec = texture(uSpectrum, vec2(uv.x, 0.0)).r;\n"
		"  float bb = texture(uBackbuffer, uv + shift).r;\n"
		"  FragColor = vec4(uv, wave * spec + bb, 1.0);\n"
		"}\n");
	fclose(f);

	Viz *good = viz_load(".tmp_files/good.frag");
	assert(good);
	assert(viz_is_loaded(good));
	assert(good->kind == VIZ_KIND_GLSL);
	assert(good->kind == VIZ_KIND_GLSL);
	assert(good->u.gl.loc_time >= 0);
	assert(good->u.gl.loc_resolution >= 0);
	assert(good->u.gl.loc_waveform >= 0);
	assert(good->u.gl.loc_spectrum >= 0);
	assert(good->u.gl.loc_backbuffer >= 0);

	FILE *b = fopen(".tmp_files/bad.frag", "w");
	fprintf(b, "this is not a shader at all\n");
	fclose(b);
	Viz *bad = viz_load(".tmp_files/bad.frag");
	assert(bad);
	assert(!viz_is_loaded(bad));
	assert(viz_error(bad)[0] != '\0');

	viz_free(good);
	viz_free(bad);
	CloseWindow();
	printf("test_viz_glsl OK\n");
	return 0;
}
