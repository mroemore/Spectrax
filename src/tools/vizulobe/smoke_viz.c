#include "vizulobe.h"
#include "raylib.h"

void viz_frame(viz_t *ctx) {
	float cx = ctx->rect_w / 2.0f;
	float cy = ctx->rect_h / 2.0f;
	float r = 40.0f + 200.0f * ctx->audio_l;
	DrawCircle((int)cx, (int)cy, r, RED);
	DrawRectangle(0, 0, (int)ctx->rect_w, (int)(ctx->audio_r * ctx->rect_h), BLUE);
}
