#include "gui_style.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Baked defaults — the current hardcoded draw-fn literals. */

static DialStyle g_defaultDial = {
	.knob   = { 20, 10, -225, 270, { 0, 0, 0, 0 }, false, { 0 }, { 0 } },
	.border = { 0.125f, 2.0f, { 0, 0, 0, 0 } },
	.value  = { "%05.2f", 38, 14, 28, 2, { 0, 0, 0, 0 } },
	.label  = { "pixel", 9, 1, -28, 18, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
};

static DialStyle g_defaultDialDiscrete = {
	.knob   = { 20, 10, -225, 270, { 0, 0, 0, 0 }, false, { 0 }, { 0 } },
	.border = { 0.125f, 2.0f, { 0, 0, 0, 0 } },
	.value  = { "%i", 10, 14, 6, 5, { 0, 0, 0, 0 } },
	.label  = { "pixel", 9, 1, 6, 21, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
};

static BtnStyle g_defaultBtn = {
	.border = { 0.125f, 2.0f, { 0, 0, 0, 0 } },
	.label  = { "pixel", 10, 1, 4, 4, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
};

static TypeLabelStyle g_defaultTypeLabel = {
	.label  = { "pixel", 10, 1, 0, 0, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
	.border = { 0.125f, 2.0f, { 0, 0, 0, 0 } },
};

/* Colour defaults are resolved against the theme at compile time; the
 * zeroed colors here get replaced by resolveDefaultColours() below. */
static void resolveDefaultColours(const ColourScheme *cs) {
	g_defaultDial.knob.color = cs->dial;
	g_defaultDial.border.color = cs->panelBorder;
	g_defaultDial.value.color = cs->valueText;
	g_defaultDial.label.color = cs->label;
	g_defaultDial.label.colorSelected = cs->labelSelected;
	g_defaultDialDiscrete.knob.color = cs->dial;
	g_defaultDialDiscrete.border.color = cs->panelBorder;
	g_defaultDialDiscrete.value.color = cs->valueText;
	g_defaultDialDiscrete.label.color = cs->label;
	g_defaultDialDiscrete.label.colorSelected = cs->labelSelected;
	g_defaultBtn.border.color = cs->panelBorder;
	g_defaultBtn.label.color = cs->label;
	g_defaultBtn.label.colorSelected = cs->labelSelected;
	g_defaultTypeLabel.label.color = cs->label;
	g_defaultTypeLabel.label.colorSelected = cs->labelSelected;
	g_defaultTypeLabel.border.color = cs->outlineColour;
}

/* Class registry: class name -> (type, index into per-type array). */
#define MAX_STYLE_CLASSES 64
#define MAX_CLASS_ENTRIES_PER_TYPE 32

typedef struct {
	char name[64];
	StyleType type;
	int index;
} StyleClassEntry;

static StyleClassEntry g_classMap[MAX_STYLE_CLASSES];
static int g_classCount = 0;

static DialStyle g_dialClasses[MAX_CLASS_ENTRIES_PER_TYPE];
static DialStyle g_discreteDialClasses[MAX_CLASS_ENTRIES_PER_TYPE];
static BtnStyle g_btnClasses[MAX_CLASS_ENTRIES_PER_TYPE];
static TypeLabelStyle g_typeLabelClasses[MAX_CLASS_ENTRIES_PER_TYPE];

static const char *g_defaultClassName(StyleType t) {
	switch(t) {
		case STYLE_DIAL:          return "dial";
		case STYLE_DIAL_DISCRETE: return "dial-discrete";
		case STYLE_BTN:           return "btn";
		case STYLE_TYPE_LABEL:    return "type-label";
		default:                  return NULL;
	}
}

static const void *g_defaultStyle(StyleType t) {
	switch(t) {
		case STYLE_DIAL:          return &g_defaultDial;
		case STYLE_DIAL_DISCRETE: return &g_defaultDialDiscrete;
		case STYLE_BTN:           return &g_defaultBtn;
		case STYLE_TYPE_LABEL:    return &g_defaultTypeLabel;
		default:                  return NULL;
	}
}

static int findClass(const char *name, StyleType want) {
	for(int i = 0; i < g_classCount; i++) {
		if(g_classMap[i].type == want && strcmp(g_classMap[i].name, name) == 0) {
			return i;
		}
	}
	return -1;
}

const DialStyle *resolveDialStyle(const GuiNode *gn) {
	if(gn && gn->className) {
		int idx = findClass(gn->className, STYLE_DIAL);
		if(idx >= 0) return &g_dialClasses[g_classMap[idx].index];
	}
	return &g_defaultDial;
}

const DialStyle *resolveDiscreteDialStyle(const GuiNode *gn) {
	if(gn && gn->className) {
		int idx = findClass(gn->className, STYLE_DIAL_DISCRETE);
		if(idx >= 0) return &g_discreteDialClasses[g_classMap[idx].index];
	}
	return &g_defaultDialDiscrete;
}

const BtnStyle *resolveBtnStyle(const GuiNode *gn) {
	if(gn && gn->className) {
		int idx = findClass(gn->className, STYLE_BTN);
		if(idx >= 0) return &g_btnClasses[g_classMap[idx].index];
	}
	return &g_defaultBtn;
}

const TypeLabelStyle *resolveTypeLabelStyle(const GuiNode *gn) {
	if(gn && gn->className) {
		int idx = findClass(gn->className, STYLE_TYPE_LABEL);
		if(idx >= 0) return &g_typeLabelClasses[g_classMap[idx].index];
	}
	return &g_defaultTypeLabel;
}

void computeDialGeometry(const GuiNode *gn, const DialStyle *st, DialGeometry *out) {
	int cx = gn->x + gn->padding;
	int cy = gn->y + gn->padding;
	out->knobX = cx + 2;
	out->knobY = cy;
	out->knobW = st->knob.size;
	out->knobH = st->knob.size;
	out->valueX = out->knobX + st->value.offsetX;
	out->valueY = out->knobY + st->value.offsetY;
	out->valueW = st->value.width;
	out->valueH = st->value.height;
	out->labelX = cx + st->label.offsetX;
	out->labelY = cy + st->label.offsetY;
}

/* compileLayoutConfig + finalizeStyles + LayoutSet: implemented in Tasks 3/6. */
bool compileLayoutConfig(const char *layoutPath, const ColourScheme *cs) {
	(void)layoutPath;
	if(cs) {
		resolveDefaultColours(cs);
	}
	return true;
}

void finalizeStyles(void) { }
