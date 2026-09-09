#include "gui_style.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "cJSON.h"
#include "io/config_io.h"

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

static int g_dialClassCount = 0;
static int g_discreteDialClassCount = 0;
static int g_btnClassCount = 0;
static int g_typeLabelClassCount = 0;

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

static StyleType typeFromName(const char *name) {
	if(!name) return STYLE_COUNT;
	if(!strcmp(name, "dial")) return STYLE_DIAL;
	if(!strcmp(name, "dial-discrete")) return STYLE_DIAL_DISCRETE;
	if(!strcmp(name, "btn")) return STYLE_BTN;
	if(!strcmp(name, "type-label")) return STYLE_TYPE_LABEL;
	return STYLE_COUNT;
}

/* Look up any registered class by name (no type filter). Returns the
 * class's StyleType via *outType; index via *outIndex; returns false
 * if `name` is not registered. */
static bool findClassAnyType(const char *name, StyleType *outType, int *outIndex) {
	for(int i = 0; i < g_classCount; i++) {
		if(strcmp(g_classMap[i].name, name) == 0) {
			if(outType) *outType = g_classMap[i].type;
			if(outIndex) *outIndex = g_classMap[i].index;
			return true;
		}
	}
	return false;
}

/* Register a freshly-classified class into the per-type array. The
 * caller has already populated g_dialClasses[*] etc. via merge into
 * the type default; we just record the mapping for findClass(). */
static bool registerClass(const char *name, StyleType type, int index) {
	if(g_classCount >= MAX_STYLE_CLASSES) return false;
	strncpy(g_classMap[g_classCount].name, name, sizeof(g_classMap[0].name) - 1);
	g_classMap[g_classCount].name[sizeof(g_classMap[0].name) - 1] = '\0';
	g_classMap[g_classCount].type = type;
	g_classMap[g_classCount].index = index;
	g_classCount++;
	return true;
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

/* ---------- JSON helpers ---------- */

static bool jsonInt(const cJSON *o, const char *k, int *dst) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	if(!cJSON_IsNumber(v)) return false;
	*dst = v->valueint;
	return true;
}

static bool jsonFloat(const cJSON *o, const char *k, float *dst) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	if(!cJSON_IsNumber(v)) return false;
	*dst = (float)v->valuedouble;
	return true;
}

static bool jsonStr(const cJSON *o, const char *k, char *dst, size_t sz) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	if(!cJSON_IsString(v) || !v->valuestring) return false;
	strncpy(dst, v->valuestring, sz - 1);
	dst[sz - 1] = '\0';
	return true;
}

/* Try to resolve `name` as a hex color; fall back to looking it up in
 * the theme's `colors` map. Returns true and fills *out on hit. */
static bool jsonColour(const cJSON *o, const char *k, const ColourScheme *cs, Color *out) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	if(!cJSON_IsString(v) || !v->valuestring) return false;
	if(parseHexColor(v->valuestring, out)) return true;
	if(cs) {
		Color *f = themeFieldByName((ColourScheme *)cs, v->valuestring);
		if(f) { *out = *f; return true; }
	}
	return false;
}

/* ---------- Per-sub-style merge ---------- */

static void mergeKnob(KnobStyle *into, const cJSON *o, const ColourScheme *cs) {
	jsonInt(o, "size", &into->size);
	jsonInt(o, "radius", &into->radius);
	jsonInt(o, "startAngle", &into->startAngle);
	jsonInt(o, "sweep", &into->sweep);
	jsonColour(o, "color", cs, &into->color);
	if(jsonStr(o, "asset", into->assetPath, sizeof(into->assetPath))) {
		into->hasAsset = true;
	}
}

static void mergeBorder(BorderStyle *into, const cJSON *o, const ColourScheme *cs) {
	jsonFloat(o, "roundness", &into->roundness);
	jsonFloat(o, "borderWidth", &into->borderWidth);
	jsonColour(o, "color", cs, &into->color);
}

static void mergeValue(ValueStyle *into, const cJSON *o, const ColourScheme *cs) {
	jsonStr(o, "format", into->format, sizeof(into->format));
	jsonInt(o, "width", &into->width);
	jsonInt(o, "height", &into->height);
	jsonInt(o, "offsetX", &into->offsetX);
	jsonInt(o, "offsetY", &into->offsetY);
	jsonColour(o, "color", cs, &into->color);
}

static void mergeLabel(LabelStyle *into, const cJSON *o, const ColourScheme *cs) {
	jsonStr(o, "font", into->fontName, sizeof(into->fontName));
	jsonInt(o, "fontSize", &into->fontSize);
	jsonInt(o, "spacing", &into->spacing);
	jsonInt(o, "offsetX", &into->offsetX);
	jsonInt(o, "offsetY", &into->offsetY);
	jsonColour(o, "color", cs, &into->color);
	jsonColour(o, "colorSelected", cs, &into->colorSelected);
}

/* ---------- Extends-chain walk + per-type merge ---------- */

/* Walk className -> $extends -> ... looking for a chain that ends at
 * a known type default ("dial" / "dial-discrete" / "btn" / "type-label")
 * or at a registered class that itself transitively reaches one.
 * Returns the type the chain lands at, plus (via *outExtends) the
 * final "extends" target name that resolved to the type — i.e. the
 * name to start the merge chain from. *outSelfRef true means the
 * chain had a cycle or was empty; the class is effectively type-less
 * and should fall back to baked defaults.
 *
 * Implementation: bounded DFS. Visited set prevents cycles. Depth cap
 * at MAX_STYLE_CLASSES so a pathological JSON can't blow the stack. */
static StyleType classifyClass(const cJSON *doc, const char *className,
		const char **outExtends, bool *outSelfRef) {
	char path[64][64];
	int depth = 0;
	const char *cur = className;
	while(cur) {
		if(depth >= 64) { *outSelfRef = true; return STYLE_COUNT; }
		strncpy(path[depth], cur, 63);
		path[depth][63] = '\0';
		for(int i = 0; i < depth; i++) {
			if(strcmp(path[i], cur) == 0) {
				*outSelfRef = true;
				return STYLE_COUNT;
			}
		}
		/* Try as type default first. */
		StyleType t = typeFromName(cur);
		if(t != STYLE_COUNT) {
			*outExtends = (depth > 0) ? path[depth - 1] : NULL;
			*outSelfRef = false;
			return t;
		}
		/* Otherwise follow $extends in the doc. */
		const cJSON *cls = NULL;
		if(depth == 0) {
			char key[80];
			snprintf(key, sizeof(key), "class:%s", cur);
			cls = cJSON_GetObjectItemCaseSensitive(doc, key);
		} else {
			cls = cJSON_GetObjectItemCaseSensitive(doc, path[depth]);
		}
		const cJSON *ext = cls ? cJSON_GetObjectItemCaseSensitive(cls, "$extends") : NULL;
		if(!cJSON_IsString(ext) || !ext->valuestring) {
			*outSelfRef = true;
			return STYLE_COUNT;
		}
		cur = ext->valuestring;
		depth++;
	}
	*outSelfRef = true;
	return STYLE_COUNT;
}

/* Allocate a per-type slot, copy the baked default in, then walk the
 * extends chain (in reverse — furthest ancestor first) merging each
 * link's overrides on top. Finally merge the leaf class itself. */
static bool compileClassEntry(const cJSON *doc, const char *className,
		const cJSON *leaf, StyleType type, const ColourScheme *cs) {
	void *slot = NULL;
	int *count = NULL;
	size_t sz = 0;
	switch(type) {
		case STYLE_DIAL:
			slot = &g_dialClasses[g_dialClassCount];
			count = &g_dialClassCount;
			sz = sizeof(DialStyle);
			break;
		case STYLE_DIAL_DISCRETE:
			slot = &g_discreteDialClasses[g_discreteDialClassCount];
			count = &g_discreteDialClassCount;
			sz = sizeof(DialStyle);
			break;
		case STYLE_BTN:
			slot = &g_btnClasses[g_btnClassCount];
			count = &g_btnClassCount;
			sz = sizeof(BtnStyle);
			break;
		case STYLE_TYPE_LABEL:
			slot = &g_typeLabelClasses[g_typeLabelClassCount];
			count = &g_typeLabelClassCount;
			sz = sizeof(TypeLabelStyle);
			break;
		default:
			return false;
	}
	if(*count >= MAX_CLASS_ENTRIES_PER_TYPE) return false;

	/* 1. Copy baked default into slot. */
	const void *baked = g_defaultStyle(type);
	memcpy(slot, baked, sz);
	int idx = (*count)++;

	/* 2. Walk extends chain from leaf backwards through doc. */
	const char *cur = className;
	char visited[16][64];
	int vcount = 0;
	while(cur) {
		if(vcount >= 16) break;
		bool seen = false;
		for(int i = 0; i < vcount; i++) {
			if(strcmp(visited[i], cur) == 0) { seen = true; break; }
		}
		if(seen) break;
		strncpy(visited[vcount], cur, 63);
		visited[vcount][63] = '\0';
		vcount++;

		char key[80];
		snprintf(key, sizeof(key), "class:%s", cur);
		const cJSON *cls = cJSON_GetObjectItemCaseSensitive(doc, key);
		if(!cls) break;
		const cJSON *ext = cJSON_GetObjectItemCaseSensitive(cls, "$extends");
		if(!cJSON_IsString(ext) || !ext->valuestring) break;
		cur = ext->valuestring;
	}
	/* Apply ancestors in order from base (first visited) toward leaf
	 * (last visited). `visited[0..vcount)` already holds base->leaf. */
	for(int i = 0; i < vcount; i++) {
		char key[80];
		snprintf(key, sizeof(key), "class:%s", visited[i]);
		const cJSON *cls = cJSON_GetObjectItemCaseSensitive(doc, key);
		if(!cls) continue;
		const cJSON *k = cJSON_GetObjectItemCaseSensitive(cls, "knob");
		const cJSON *b = cJSON_GetObjectItemCaseSensitive(cls, "border");
		const cJSON *v = cJSON_GetObjectItemCaseSensitive(cls, "value");
		const cJSON *l = cJSON_GetObjectItemCaseSensitive(cls, "label");
		if(type == STYLE_DIAL || type == STYLE_DIAL_DISCRETE) {
			DialStyle *d = (DialStyle *)slot;
			if(cJSON_IsObject(k)) mergeKnob(&d->knob, k, cs);
			if(cJSON_IsObject(b)) mergeBorder(&d->border, b, cs);
			if(cJSON_IsObject(v)) mergeValue(&d->value, v, cs);
			if(cJSON_IsObject(l)) mergeLabel(&d->label, l, cs);
		} else if(type == STYLE_BTN) {
			BtnStyle *bs = (BtnStyle *)slot;
			if(cJSON_IsObject(b)) mergeBorder(&bs->border, b, cs);
			if(cJSON_IsObject(l)) mergeLabel(&bs->label, l, cs);
		} else if(type == STYLE_TYPE_LABEL) {
			TypeLabelStyle *ts = (TypeLabelStyle *)slot;
			if(cJSON_IsObject(l)) mergeLabel(&ts->label, l, cs);
			if(cJSON_IsObject(b)) mergeBorder(&ts->border, b, cs);
		}
	}

	/* 3. Apply the leaf class's own overrides on top. */
	const cJSON *k = cJSON_GetObjectItemCaseSensitive(leaf, "knob");
	const cJSON *b = cJSON_GetObjectItemCaseSensitive(leaf, "border");
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(leaf, "value");
	const cJSON *l = cJSON_GetObjectItemCaseSensitive(leaf, "label");
	if(type == STYLE_DIAL || type == STYLE_DIAL_DISCRETE) {
		DialStyle *d = (DialStyle *)slot;
		if(cJSON_IsObject(k)) mergeKnob(&d->knob, k, cs);
		if(cJSON_IsObject(b)) mergeBorder(&d->border, b, cs);
		if(cJSON_IsObject(v)) mergeValue(&d->value, v, cs);
		if(cJSON_IsObject(l)) mergeLabel(&d->label, l, cs);
	} else if(type == STYLE_BTN) {
		BtnStyle *bs = (BtnStyle *)slot;
		if(cJSON_IsObject(b)) mergeBorder(&bs->border, b, cs);
		if(cJSON_IsObject(l)) mergeLabel(&bs->label, l, cs);
	} else if(type == STYLE_TYPE_LABEL) {
		TypeLabelStyle *ts = (TypeLabelStyle *)slot;
		if(cJSON_IsObject(l)) mergeLabel(&ts->label, l, cs);
		if(cJSON_IsObject(b)) mergeBorder(&ts->border, b, cs);
	}

	if(!registerClass(className, type, idx)) {
		(*count)--;
		return false;
	}
	return true;
}

/* ---------- Compile entry point ---------- */

bool compileLayoutConfig(const char *layoutPath, const ColourScheme *cs) {
	if(cs) {
		resolveDefaultColours(cs);
	}
	if(!layoutPath) return true;

	FILE *f = fopen(layoutPath, "rb");
	if(!f) return false;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if(sz <= 0 || sz > 1 << 20) { fclose(f); return false; }
	char *buf = malloc((size_t)sz + 1);
	if(!buf) { fclose(f); return false; }
	if(fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return false; }
	buf[sz] = '\0';
	fclose(f);

	cJSON *doc = cJSON_Parse(buf);
	free(buf);
	if(!doc) return false;

	/* Root-level `$extends` is honoured as a class's own default type
	 * when a class:foo entry doesn't override it. Walk every "class:X"
	 * child. */
	cJSON *child = NULL;
	cJSON_ArrayForEach(child, doc) {
		const char *key = child->string;
		if(!key) continue;
		if(strncmp(key, "class:", 6) != 0) continue;
		const char *className = key + 6;
		if(!cJSON_IsObject(child)) continue;

		/* Determine type: leaf "type" key wins; otherwise walk extends
		 * chain to classify. */
		StyleType type = STYLE_COUNT;
		const cJSON *typeObj = cJSON_GetObjectItemCaseSensitive(child, "type");
		if(cJSON_IsString(typeObj) && typeObj->valuestring) {
			type = typeFromName(typeObj->valuestring);
		}
		if(type == STYLE_COUNT) {
			bool selfRef = false;
			const char *unused = NULL;
			type = classifyClass(doc, className, &unused, &selfRef);
			if(type == STYLE_COUNT || selfRef) {
				/* Unknown class — fall back to baked default by
				 * mapping to STYLE_DIAL just to allocate a slot
				 * whose baked defaults hold. Actually simpler: just
				 * skip registering; resolve*() will return baked. */
				continue;
			}
		}
		compileClassEntry(doc, className, child, type, cs);
	}
	cJSON_Delete(doc);
	return true;
}

void finalizeStyles(void) { }
