#ifndef GUI_STYLE_H
#define GUI_STYLE_H

#include <stdbool.h>
#include "graph_gui.h"
#include "theme.h"
#include "raylib.h"

/* Sub-element styles — one per drawable region of a widget. Baked
 * defaults equal the current hardcoded draw-fn literals. */

typedef struct {
	int size;
	int radius;
	int startAngle;
	int sweep;
	int offsetX;
	int offsetY;
	Color color;
	bool hasAsset;
	char assetPath[256];
	Texture2D assetTex;
} KnobStyle;

typedef struct {
	float roundness;
	float borderWidth;
	Color color;
} BorderStyle;

typedef struct {
	char format[16];
	int width;
	int height;
	int offsetX;
	int offsetY;
	Color color;
} ValueStyle;

typedef struct {
	char fontName[16];
	int fontSize;
	int spacing;
	int offsetX;
	int offsetY;
	Color color;
	Color colorSelected;
} LabelStyle;

typedef struct {
	KnobStyle knob;
	BorderStyle border;
	ValueStyle value;
	LabelStyle label;
} DialStyle;

typedef struct {
	BorderStyle border;
	LabelStyle label;
} BtnStyle;

typedef struct {
	LabelStyle label;
	BorderStyle border;
} TypeLabelStyle;

typedef enum {
	STYLE_DIAL,
	STYLE_DIAL_DISCRETE,
	STYLE_BTN,
	STYLE_TYPE_LABEL,
	STYLE_COUNT
} StyleType;

void guiNodeSetClass(GuiNode *gn, const char *className);

const DialStyle *resolveDialStyle(const GuiNode *gn);
const DialStyle *resolveDiscreteDialStyle(const GuiNode *gn);
const BtnStyle *resolveBtnStyle(const GuiNode *gn);
const TypeLabelStyle *resolveTypeLabelStyle(const GuiNode *gn);

Font *styleFont(const char *name);

typedef struct {
	int knobX, knobY, knobW, knobH;
	int valueX, valueY, valueW, valueH;
	int labelX, labelY;
} DialGeometry;

void computeDialGeometry(const GuiNode *gn, const DialStyle *st, DialGeometry *out);

bool compileLayoutConfig(const char *layoutPath, const ColourScheme *cs);
void finalizeStyles(void);

typedef struct {
	int orientation;
	int padding;
	int weightCount;
	int weights[MAX_NODE_CHILDREN];
	int childClassCount;
	const char *childClasses[MAX_NODE_CHILDREN];
} LayoutDef;

const LayoutDef *layoutByName(const char *name);
void applyLayout(GuiNode *container, const char *name);

#endif
