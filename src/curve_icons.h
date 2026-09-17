#ifndef CURVE_ICONS_H
#define CURVE_ICONS_H

#include "raylib.h"

#define CURVE_ICON_COUNT 32
#define CURVE_ICON_SIZE 8

int curveIconIndex(float curvature);
Texture2D buildCurveIconTexture(void);

#endif