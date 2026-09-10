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
	Color colorSelected;
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

typedef struct {
	BorderStyle border;
} DestStyle;

/* Pattern-screen step cell (one of MAX_SEQUENCER_STEPS in a single
 * channel row). State-variant background colour is the cell's defining
 * trait: bgEmpty when no note is set, bgPlaying when the playhead is
 * hitting this step. borderSelected is drawn as the surround when the
 * cell is the selected step in the pattern editor. The note LabelStyle
 * is the pitch letter rendered at the cell centre; it uses the named
 * "text" font slot so it shares the global textFont. */
typedef struct {
	Color bgEmpty;
	Color bgPlaying;
	Color borderSelected;
	LabelStyle note;
} StepCellStyle;

typedef enum {
	STYLE_DIAL,
	STYLE_DIAL_DISCRETE,
	STYLE_BTN,
	STYLE_TYPE_LABEL,
	STYLE_STEP_CELL,
	STYLE_DEST,
	STYLE_COUNT
} StyleType;

void guiNodeSetClass(GuiNode *gn, const char *className);

const DialStyle *resolveDialStyle(const GuiNode *gn);
const DialStyle *resolveDiscreteDialStyle(const GuiNode *gn);
const BtnStyle *resolveBtnStyle(const GuiNode *gn);
const TypeLabelStyle *resolveTypeLabelStyle(const GuiNode *gn);
const StepCellStyle *resolveStepCellStyle(const GuiNode *gn);
const DestStyle *resolveDestStyle(const GuiNode *gn);

Font *styleFont(const char *name);

typedef struct {
	int knobX, knobY, knobW, knobH;
	int valueX, valueY, valueW, valueH;
	int labelX, labelY;
} DialGeometry;

void computeDialGeometry(const GuiNode *gn, const DialStyle *st, DialGeometry *out);

/* The dial component's natural content height: the lowest extent of
 * knob, value and label (all anchored at the knob top). Does not
 * include cell padding — add 2 * gn->padding for the full cell. */
int dialComponentHeight(const DialStyle *st);

bool compileLayoutConfig(const char *layoutPath, const ColourScheme *cs);
void finalizeStyles(void);

typedef struct {
	int orientation;
	int padding;
	int gap;
	int weightCount;
	int weights[MAX_NODE_CHILDREN];
	int childClassCount;
	const char *childClasses[MAX_NODE_CHILDREN];
} LayoutDef;

const LayoutDef *layoutByName(const char *name);
void applyLayout(GuiNode *container, const char *name);

#endif
