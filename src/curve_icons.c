#include "curve_icons.h"
#include "modsystem.h"

#include <math.h>

int curveIconIndex(float curvature) {
	int idx = (int)lroundf(curvature * (CURVE_ICON_COUNT - 1));
	if(idx < 0) idx = 0;
	if(idx > CURVE_ICON_COUNT - 1) idx = CURVE_ICON_COUNT - 1;
	return idx;
}

Texture2D buildCurveIconTexture(void) {
	const int frameW = CURVE_ICON_SIZE;
	const int atlasW = CURVE_ICON_COUNT * frameW;
	const int atlasH = CURVE_ICON_SIZE;
	Image img = GenImageColor(atlasW, atlasH, (Color){ 0, 0, 0, 0 });
	Color *px = (Color *)img.data;
	for(int f = 0; f < CURVE_ICON_COUNT; f++) {
		float samples[CURVE_ICON_SIZE];
		generateCurve(samples, CURVE_ICON_SIZE, (float)f / (CURVE_ICON_COUNT - 1), 1);
		const int fx0 = f * frameW;
		for(int x = 0; x < CURVE_ICON_SIZE; x++) {
			int y = (int)(samples[x] * (CURVE_ICON_SIZE - 1));
			if(y < 0) y = 0;
			if(y > CURVE_ICON_SIZE - 1) y = CURVE_ICON_SIZE - 1;
			px[fx0 + x + y * atlasW] = WHITE;
		}
	}
	Texture2D tex = LoadTextureFromImage(img);
	UnloadImage(img);
	SetTextureFilter(tex, TEXTURE_FILTER_POINT);
	return tex;
}