#include "gui_style.h"
#include "cJSON.h"
#include "io/config_io.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Layout storage — named LayoutDef + childClasses buffer. The
 * childClasses table points into g_childClassBuf so the strings survive
 * until the next compileLayoutConfig call. */
#define MAX_LAYOUTS 32

static LayoutDef g_layouts[MAX_LAYOUTS];
static int g_layoutCount = 0;
static char g_layoutNames[MAX_LAYOUTS][64];
static char g_childClassBuf[MAX_LAYOUTS][MAX_NODE_CHILDREN][64];

const LayoutDef *layoutByName(const char *name) {
	if(!name) {
		return NULL;
	}
	for(int i = 0; i < g_layoutCount; i++) {
		if(strcmp(g_layoutNames[i], name) == 0) {
			return &g_layouts[i];
		}
	}
	return NULL;
}

void applyLayout(GuiNode *container, const char *name) {
	const LayoutDef *ld = layoutByName(name);
	if(!ld || !container) {
		return;
	}
	container->nodeAlignment = (uint8_t)ld->orientation;
	container->padding = (uint16_t)ld->padding;

	int total = 0;
	ListElement *wcur = container->itemWeights ? container->itemWeights->head : NULL;
	ListElement *ccur = container->items ? container->items->head : NULL;
	for(int i = 0; i < container->itemCount && wcur && ccur; i++, wcur = wcur->next, ccur = ccur->next) {
		int w = (i < ld->weightCount) ? ld->weights[i] : 1;
		if(w < 1) {
			w = 1;
		}
		*(int *)wcur->data = w;
		total += w;
		if(i < ld->childClassCount && ld->childClasses[i]) {
			GuiNode *child = *(GuiNode **)ccur->data;
			guiNodeSetClass(child, ld->childClasses[i]);
		}
	}
	container->totalItemWeights = (uint32_t)total;
	reflowCoordinates(container);
}

/* Baked defaults — the current hardcoded draw-fn literals. */

static DialStyle g_defaultDial = {
	.knob   = { 20, 10, -225, 270, 0, 0, { 0, 0, 0, 0 }, false, { 0 }, { 0 } },
	.border = { 0.125f, 2.0f, { 0, 0, 0, 0 } },
	.value  = { "%05.2f", 38, 14, 28, 2, { 0, 0, 0, 0 } },
	.label  = { "pixel", 9, 1, 0, 18, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
};

static DialStyle g_defaultDialDiscrete = {
	.knob   = { 20, 10, -225, 270, 0, 0, { 0, 0, 0, 0 }, false, { 0 }, { 0 } },
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

/* Per-type entry counts. Count the "dial" / "dial-discrete" / "btn" /
 * "type-label" defaults themselves as the first entry so custom classes
 * are zero-indexed on top. */
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

int dialComponentHeight(const DialStyle *st) {
	if(!st) {
		return 0;
	}
	int h = st->knob.size;
	int v = st->value.offsetY + st->value.height;
	int l = st->label.offsetY + st->label.fontSize;
	if(v > h) {
		h = v;
	}
	if(l > h) {
		h = l;
	}
	return h;
}

void computeDialGeometry(const GuiNode *gn, const DialStyle *st, DialGeometry *out) {
	int cx = gn->x + gn->padding;
	int cy = gn->y + gn->padding;
	int contentW = (int)gn->w - 2 * (int)gn->padding;
	int contentH = (int)gn->h - 2 * (int)gn->padding;
	/* Mini-layout: the trio [knob, value, label] is a composition
	 * anchored on the knob. The knob centers in the cell on both axes
	 * when there is slack; value + label follow the knob (their
	 * offsets are knob-relative), so the whole unit moves as one. */
	int groupW = st->knob.size + st->value.offsetX + st->value.width;
	int knobX = (groupW < contentW) ? (cx + (contentW - groupW) / 2) : (cx + 2);
	int knobY = (st->knob.size < contentH) ? (cy + (contentH - st->knob.size) / 2) : cy;
	out->knobX = knobX;
	out->knobY = knobY;
	out->knobW = st->knob.size;
	out->knobH = st->knob.size;
	out->valueX = knobX + st->value.offsetX;
	out->valueY = knobY + st->value.offsetY;
	out->valueW = st->value.width;
	out->valueH = st->value.height;
	/* The label is the dial's caption: it centres in the cell (offsetX
	 * is a fine-tune from the cell centre) and sits below the knob
	 * (offsetY relative to the knob top). */
	out->labelX = cx + contentW / 2 + st->label.offsetX;
	out->labelY = knobY + st->label.offsetY;
}

/* --- layout.json compile ---------------------------------------------- */

/* Returns cJSON_IsNumber(v) ? v->valueint : def. */
static int jsonInt(cJSON *o, const char *k, int def) {
	cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	return (v && cJSON_IsNumber(v)) ? v->valueint : def;
}
static float jsonFloat(cJSON *o, const char *k, float def) {
	cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	return (v && cJSON_IsNumber(v)) ? (float)v->valuedouble : def;
}
/* Color: theme field name only (no inline hex — palette lives in clr.json). */
static void jsonColor(cJSON *o, const char *k, const ColourScheme *cs, Color *out) {
	cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	if(v && cJSON_IsString(v)) {
		Color *field = themeFieldByName((ColourScheme *)cs, v->valuestring);
		if(field) {
			*out = *field;
		}
	}
}
static void jsonStr(cJSON *o, const char *k, char *out, size_t sz) {
	cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	if(v && cJSON_IsString(v)) {
		strncpy(out, v->valuestring, sz - 1);
		out[sz - 1] = '\0';
	}
}

static void overlayKnob(cJSON *o, const ColourScheme *cs, KnobStyle *k) {
	k->size = jsonInt(o, "size", k->size);
	k->radius = jsonInt(o, "radius", k->radius);
	k->startAngle = jsonInt(o, "startAngle", k->startAngle);
	k->sweep = jsonInt(o, "sweep", k->sweep);
	k->offsetX = jsonInt(o, "offsetX", k->offsetX);
	k->offsetY = jsonInt(o, "offsetY", k->offsetY);
	jsonColor(o, "color", cs, &k->color);
	jsonStr(o, "asset", k->assetPath, sizeof(k->assetPath));
	if(k->assetPath[0]) {
		k->hasAsset = true;
	}
}
static void overlayBorder(cJSON *o, const ColourScheme *cs, BorderStyle *b) {
	b->roundness = jsonFloat(o, "roundness", b->roundness);
	b->borderWidth = jsonFloat(o, "borderWidth", b->borderWidth);
	jsonColor(o, "color", cs, &b->color);
}
static void overlayValue(cJSON *o, const ColourScheme *cs, ValueStyle *v) {
	jsonStr(o, "format", v->format, sizeof(v->format));
	v->width = jsonInt(o, "width", v->width);
	v->height = jsonInt(o, "height", v->height);
	v->offsetX = jsonInt(o, "offsetX", v->offsetX);
	v->offsetY = jsonInt(o, "offsetY", v->offsetY);
	jsonColor(o, "color", cs, &v->color);
}
static void overlayLabel(cJSON *o, const ColourScheme *cs, LabelStyle *l) {
	jsonStr(o, "font", l->fontName, sizeof(l->fontName));
	l->fontSize = jsonInt(o, "fontSize", l->fontSize);
	l->spacing = jsonInt(o, "spacing", l->spacing);
	l->offsetX = jsonInt(o, "offsetX", l->offsetX);
	l->offsetY = jsonInt(o, "offsetY", l->offsetY);
	jsonColor(o, "color", cs, &l->color);
	jsonColor(o, "colorSelected", cs, &l->colorSelected);
}

static void overlayDial(cJSON *o, const ColourScheme *cs, DialStyle *d) {
	cJSON *knob = cJSON_GetObjectItemCaseSensitive(o, "knob");
	if(cJSON_IsObject(knob)) overlayKnob(knob, cs, &d->knob);
	cJSON *border = cJSON_GetObjectItemCaseSensitive(o, "border");
	if(cJSON_IsObject(border)) overlayBorder(border, cs, &d->border);
	cJSON *value = cJSON_GetObjectItemCaseSensitive(o, "value");
	if(cJSON_IsObject(value)) overlayValue(value, cs, &d->value);
	cJSON *label = cJSON_GetObjectItemCaseSensitive(o, "label");
	if(cJSON_IsObject(label)) overlayLabel(label, cs, &d->label);
}
static void overlayBtn(cJSON *o, const ColourScheme *cs, BtnStyle *b) {
	cJSON *border = cJSON_GetObjectItemCaseSensitive(o, "border");
	if(cJSON_IsObject(border)) overlayBorder(border, cs, &b->border);
	cJSON *label = cJSON_GetObjectItemCaseSensitive(o, "label");
	if(cJSON_IsObject(label)) overlayLabel(label, cs, &b->label);
}
static void overlayTypeLabel(cJSON *o, const ColourScheme *cs, TypeLabelStyle *t) {
	cJSON *label = cJSON_GetObjectItemCaseSensitive(o, "label");
	if(cJSON_IsObject(label)) overlayLabel(label, cs, &t->label);
	cJSON *border = cJSON_GetObjectItemCaseSensitive(o, "border");
	if(cJSON_IsObject(border)) overlayBorder(border, cs, &t->border);
}

static StyleType typeDefaultForName(const char *name) {
	for(int t = 0; t < STYLE_COUNT; t++) {
		const char *def = g_defaultClassName((StyleType)t);
		if(def && strcmp(def, name) == 0) {
			return (StyleType)t;
		}
	}
	return STYLE_COUNT;
}

/* Follow `extends` (via the parsed styles object) to classify a class.
 * A class is typed when it IS a known type default or transitively
 * extends one. */
static StyleType classifyClass(cJSON *styles, const char *name, cJSON *obj) {
	StyleType t = typeDefaultForName(name);
	if(t != STYLE_COUNT) {
		return t;
	}
	for(int depth = 0; depth < 16; depth++) {
		cJSON *e = cJSON_GetObjectItemCaseSensitive(obj, "extends");
		if(!cJSON_IsString(e)) {
			return STYLE_COUNT;
		}
		const char *parent = e->valuestring;
		t = typeDefaultForName(parent);
		if(t != STYLE_COUNT) {
			return t;
		}
		obj = cJSON_GetObjectItemCaseSensitive(styles, parent);
		if(!obj) {
			return STYLE_COUNT;
		}
	}
	return STYLE_COUNT;
}

/* Collect the JSON chain root->leaf (baked default implied at index 0).
 * Even when `name` is a type default, we still walk `extends` so e.g.
 * `dial-discrete extends dial` picks up the dial layer first. */
static int collectChain(cJSON *styles, const char *name, cJSON *obj, cJSON **out, int maxN) {
	cJSON *chain[16];
	int n = 0;
	for(int depth = 0; depth < 16; depth++) {
		chain[n++] = obj;
		cJSON *e = cJSON_GetObjectItemCaseSensitive(obj, "extends");
		if(!cJSON_IsString(e)) {
			break;
		}
		const char *parent = e->valuestring;
		obj = cJSON_GetObjectItemCaseSensitive(styles, parent);
		if(!obj) {
			/* Parent named in extends isn't in this styles object; treat
			 * baked default as implicit base. */
			break;
		}
	}
	if(n > maxN) n = maxN;
	for(int i = 0; i < n; i++) {
		out[i] = chain[n - 1 - i];
	}
	return n;
}

bool compileLayoutConfig(const char *layoutPath, const ColourScheme *cs) {
	g_classCount = 0;
	if(cs) {
		resolveDefaultColours(cs);
	}
	if(!layoutPath) {
		return false;
	}
	FILE *f = fopen(layoutPath, "rb");
	if(!f) {
		return false;
	}
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if(sz <= 0 || sz > 1 << 20) {
		fclose(f);
		return false;
	}
	char *buf = malloc((size_t)sz + 1);
	if(!buf) {
		fclose(f);
		return false;
	}
	if(fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
		free(buf);
		fclose(f);
		return false;
	}
	buf[sz] = '\0';
	fclose(f);

	cJSON *doc = cJSON_Parse(buf);
	free(buf);
	if(!doc) {
		return false;
	}

	cJSON *styles = cJSON_GetObjectItemCaseSensitive(doc, "styles");
	if(cJSON_IsObject(styles)) {
		cJSON *item = NULL;
		cJSON_ArrayForEach(item, styles) {
			const char *name = item->string;
			StyleType type = classifyClass(styles, name, item);
			if(type == STYLE_COUNT || !cJSON_IsObject(item)) {
				continue;
			}
			cJSON *chain[16];
			int n = collectChain(styles, name, item, chain, 16);
			if(n <= 0) {
				continue;
			}
			switch(type) {
				case STYLE_DIAL: {
					DialStyle merged = g_defaultDial;
					for(int i = 0; i < n; i++) {
						overlayDial(chain[i], cs, &merged);
					}
					int idx = g_dialClassCount < MAX_CLASS_ENTRIES_PER_TYPE ? g_dialClassCount++ : -1;
					if(idx < 0 || g_classCount >= MAX_STYLE_CLASSES) {
						continue;
					}
					g_dialClasses[idx] = merged;
					strncpy(g_classMap[g_classCount].name, name, 63);
					g_classMap[g_classCount].name[63] = '\0';
					g_classMap[g_classCount].type = STYLE_DIAL;
					g_classMap[g_classCount].index = idx;
					g_classCount++;
					break;
				}
				case STYLE_DIAL_DISCRETE: {
					DialStyle merged = g_defaultDialDiscrete;
					for(int i = 0; i < n; i++) {
						overlayDial(chain[i], cs, &merged);
					}
					int idx = g_discreteDialClassCount < MAX_CLASS_ENTRIES_PER_TYPE ? g_discreteDialClassCount++ : -1;
					if(idx < 0 || g_classCount >= MAX_STYLE_CLASSES) {
						continue;
					}
					g_discreteDialClasses[idx] = merged;
					strncpy(g_classMap[g_classCount].name, name, 63);
					g_classMap[g_classCount].name[63] = '\0';
					g_classMap[g_classCount].type = STYLE_DIAL_DISCRETE;
					g_classMap[g_classCount].index = idx;
					g_classCount++;
					break;
				}
				case STYLE_BTN: {
					BtnStyle merged = g_defaultBtn;
					for(int i = 0; i < n; i++) {
						overlayBtn(chain[i], cs, &merged);
					}
					int idx = g_btnClassCount < MAX_CLASS_ENTRIES_PER_TYPE ? g_btnClassCount++ : -1;
					if(idx < 0 || g_classCount >= MAX_STYLE_CLASSES) {
						continue;
					}
					g_btnClasses[idx] = merged;
					strncpy(g_classMap[g_classCount].name, name, 63);
					g_classMap[g_classCount].name[63] = '\0';
					g_classMap[g_classCount].type = STYLE_BTN;
					g_classMap[g_classCount].index = idx;
					g_classCount++;
					break;
				}
				case STYLE_TYPE_LABEL: {
					TypeLabelStyle merged = g_defaultTypeLabel;
					for(int i = 0; i < n; i++) {
						overlayTypeLabel(chain[i], cs, &merged);
					}
					int idx = g_typeLabelClassCount < MAX_CLASS_ENTRIES_PER_TYPE ? g_typeLabelClassCount++ : -1;
					if(idx < 0 || g_classCount >= MAX_STYLE_CLASSES) {
						continue;
					}
					g_typeLabelClasses[idx] = merged;
					strncpy(g_classMap[g_classCount].name, name, 63);
					g_classMap[g_classCount].name[63] = '\0';
					g_classMap[g_classCount].type = STYLE_TYPE_LABEL;
					g_classMap[g_classCount].index = idx;
					g_classCount++;
					break;
				}
				default:
					break;
			}
		}
	}
	cJSON *layouts = cJSON_GetObjectItemCaseSensitive(doc, "layouts");
	if(cJSON_IsObject(layouts)) {
		cJSON *item = NULL;
		cJSON_ArrayForEach(item, layouts) {
			if(g_layoutCount >= MAX_LAYOUTS) {
				break;
			}
			const char *name = item->string;
			cJSON *orient = cJSON_GetObjectItemCaseSensitive(item, "orientation");
			const char *orientStr = cJSON_IsString(orient) ? orient->valuestring : "horizontal";
			int orientVal = (strcmp(orientStr, "vertical") == 0) ? na_vertical : na_horizontal;

			LayoutDef *ld = &g_layouts[g_layoutCount];
			memset(ld, 0, sizeof(*ld));
			ld->orientation = orientVal;
			ld->padding = jsonInt(item, "padding", 0);

			cJSON *weights = cJSON_GetObjectItemCaseSensitive(item, "weights");
			if(cJSON_IsArray(weights)) {
				int n = cJSON_GetArraySize(weights);
				if(n > MAX_NODE_CHILDREN) n = MAX_NODE_CHILDREN;
				for(int i = 0; i < n; i++) {
					cJSON *w = cJSON_GetArrayItem(weights, i);
					if(cJSON_IsNumber(w)) {
						ld->weights[ld->weightCount++] = w->valueint > 0 ? w->valueint : 1;
					}
				}
			}

			cJSON *cc = cJSON_GetObjectItemCaseSensitive(item, "childClasses");
			if(cJSON_IsArray(cc)) {
				int n = cJSON_GetArraySize(cc);
				if(n > MAX_NODE_CHILDREN) n = MAX_NODE_CHILDREN;
				for(int i = 0; i < n; i++) {
					cJSON *e = cJSON_GetArrayItem(cc, i);
					if(cJSON_IsString(e) && e->valuestring[0]) {
						strncpy(g_childClassBuf[g_layoutCount][ld->childClassCount],
						        e->valuestring, 63);
						g_childClassBuf[g_layoutCount][ld->childClassCount][63] = '\0';
						ld->childClasses[ld->childClassCount] =
						  g_childClassBuf[g_layoutCount][ld->childClassCount];
						ld->childClassCount++;
					} else {
						ld->childClasses[ld->childClassCount++] = NULL;
					}
				}
			}

			strncpy(g_layoutNames[g_layoutCount], name, 63);
			g_layoutNames[g_layoutCount][63] = '\0';
			g_layoutCount++;
		}
	}

	cJSON_Delete(doc);
	return true;
}

/* Fonts resolve post-InitGUI; asset textures load here too. Knob asset
 * loading: LoadTexture(assetPath) when hasAsset; zero id on failure
 * clears hasAsset. */
extern Font pixelFont;
extern Font textFont;
extern Font symbolFont;

void finalizeStyles(void) {
	/* Load each dial class's knob texture (asset path from layout.json).
	 * Runs after InitGUI so raylib is ready; a failed load leaves
	 * assetTex.id == 0 and the draw falls back to the procedural
	 * disc/arc. */
	for(int i = 0; i < g_dialClassCount; i++) {
		if(g_dialClasses[i].knob.assetPath[0] && g_dialClasses[i].knob.assetTex.id == 0) {
			g_dialClasses[i].knob.assetTex = LoadTexture(g_dialClasses[i].knob.assetPath);
		}
	}
	for(int i = 0; i < g_discreteDialClassCount; i++) {
		if(g_discreteDialClasses[i].knob.assetPath[0] && g_discreteDialClasses[i].knob.assetTex.id == 0) {
			g_discreteDialClasses[i].knob.assetTex = LoadTexture(g_discreteDialClasses[i].knob.assetPath);
		}
	}
	(void)pixelFont; (void)textFont; (void)symbolFont;
}
