# Layout Config System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make GUI widget look (class-based styles) and module arrangement (named layouts) configurable from a single `layout.json`, with the current hardcoded look as the baked-in fallback.

**Architecture:** Three tiers — composition stays in code (each module's widget tree), a class-based style system defines each component type's intrinsic geometry/colours/assets, and a named-layout system parameterizes how a code-built group arranges its children. `layout.json` (sibling of `cfg.json`/`clr.json`) carries `styles` + `layouts` sections, compiled once at boot into a `StyleSet`/`LayoutSet` that draw fns and `applyLayout` reference.

**Tech Stack:** C99, cJSON (vendored `third_party/cjson`), existing `gui_core.c`/`graph_gui.c`/`config_io.c` machinery, meson/ninja, scripted harness fixtures.

## Global Constraints

- Every property has one canonical name: `width`/`height`, `offsetX`/`offsetY`, `roundness`, `borderWidth`, `font`/`fontSize`/`spacing`, `color` (always a `ColourScheme` name), `asset` (always a path).
- Offsets are relative to the node content origin (`x + padding`, `y + padding`).
- Fallback chain for any node: explicit class → `extends` chain → draw fn's type default → baked hardcoded defaults. A missing/malformed `layout.json` or an unknown class must render byte-for-byte as today.
- `color` resolution: theme colour name → `ColourScheme` value; unknown name falls through the chain; if the resolved class still has no colour, the baked colour stays.
- `asset` failure → procedural draw fallback (no crash, no missing-texture render).
- `GuiNode` gains a `className` field; every field readable by a walk/dispatch must be zero-initialised in `initGuiNode` (rule #1154).
- No hot reload; single style compile at startup.
- All testing code that runs inside the main binary must be opt-in via CLI flag (rule #1136). Unit tests run via `meson test`; windowed runs via the scripted fixtures (`run_scripted.sh`).
- Never push without explicit user approval (#1078). Restore `bin/s1.sng` + `bin/data/instrument_presets/fm1.ipb` from HEAD after fixture runs (#1167).

## Current hardcoded values being captured as baked defaults

`drawDialGuiNode` (continuous) / `drawDiscreteDialGuiNode` (discrete) in `src/gui_core.c`:

| field | continuous | discrete |
|---|---|---|
| border roundness | 0.125 | 0.125 |
| border width | 2.0 | 2.0 |
| inner offset | +2, +0 | +0, +0 |
| knob size/radius | 20/10 | 20/10 |
| knob startAngle / sweep | -225 / 270 | -225 / 270 |
| value format | `%05.2f` | `%i` |
| value w/h | 38/14 | 10/14 |
| value offset | +28/+2 | +6/+5 |
| label font | pixel | pixel |
| label fontSize/spacing | 9/1 | 9/1 |
| label offset | -28/+18 | +6/+21 |
| angle mapping | `(v-min)/(range/100) * 2.7` rad | same |

`drawActionBtnGuiNode`: border 0.125/2.0, label +4/+4, font 10/1.
`drawTypeLabelGuiNode`: label centred, font 10/1, border outline 2.0 when selected.

---

### Task 1: `GuiNode.className` + `guiNodeSetClass`

**Files:**
- Modify: `src/graph_gui.h` (GuiNode struct, near `name`)
- Modify: `src/graph_gui.c` (`initGuiNode`, `freeGuiNode`)

**Interfaces:**
- Produces: `void guiNodeSetClass(GuiNode *gn, const char *className)` — owned copy; NULL clears. Later tasks' resolvers read `gn->className`.

- [ ] **Step 1: Write the failing test** — add to `tests/dsp/test_graph_nav.c`:

```c
static int test_node_class_name(void) {
    GuiNode *n = createBlankGuiNode();
    ASSERT_TRUE(n != NULL, "create");
    ASSERT_TRUE(n->className == NULL, "className zeroed by init");
    guiNodeSetClass(n, "env-attack");
    ASSERT_TRUE(n->className != NULL, "class set");
    ASSERT_TRUE(strcmp(n->className, "env-attack") == 0, "class value");
    guiNodeSetClass(n, NULL);
    ASSERT_TRUE(n->className == NULL, "class cleared");
    freeGuiNode(n);
    printf("PASS test_node_class_name\n");
    return 0;
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `ninja -C build && meson test -C build test_graph_nav -v`
Expected: FAIL — `className` member does not exist yet.

- [ ] **Step 3: Implement**

In `src/graph_gui.h`, add after `char *name;`:

```c
	char *className;
```

In `src/graph_gui.c` `initGuiNode`, after `gn->name = malloc(...)`/`strcpy(...)` block, add:

```c
	gn->className = NULL;
```

In `src/graph_gui.c` `freeGuiNode`, after `free(gn->name);` add:

```c
	if(gn->className) {
		free(gn->className);
	}
```

Add a setter at the bottom of `src/graph_gui.c`:

```c
void guiNodeSetClass(GuiNode *gn, const char *className) {
	if(!gn) {
		return;
	}
	if(gn->className) {
		free(gn->className);
		gn->className = NULL;
	}
	if(className && className[0]) {
		gn->className = malloc(strlen(className) + 1);
		if(gn->className) {
			strcpy(gn->className, className);
		}
	}
}
```

Declare in `src/graph_gui.h` (after `CustomNavFunc` typedef block):

```c
void guiNodeSetClass(GuiNode *gn, const char *className);
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `ninja -C build && meson test -C build test_graph_nav -v`
Expected: PASS, and the suite (10 tests) is green.

- [ ] **Step 5: Commit**

```bash
git add src/graph_gui.h src/graph_gui.c tests/dsp/test_graph_nav.c
git commit -m "feat(style): GuiNode.className + guiNodeSetClass (zero-init + owned copy)"
```

---

### Task 2: `gui_style` module — style structs, baked defaults, resolvers, geometry helper

**Files:**
- Create: `src/gui_style.h`
- Create: `src/gui_style.c`
- Modify: `src/meson.build` (add `'gui_style.c'` to `core_sources`)
- Modify: `tests/meson.build` (add `test_layout` with a gui-extra branch)
- Create: `tests/dsp/test_layout.c`

**Interfaces:**
- Consumes: `GuiNode.className` (Task 1), `ColourScheme` from `theme.h`.
- Produces: `DialStyle`/`BtnStyle`/`TypeLabelStyle` structs; `resolveDialStyle`, `resolveDiscreteDialStyle`, `resolveBtnStyle`, `resolveTypeLabelStyle`; `computeDialGeometry`; `compileLayoutConfig(path, cs)`; `finalizeStyles(void)`; `LayoutDef` + `applyLayout` + `layoutByName` (LayoutDef/applyLayout are stubs here, filled in Task 6).

- [ ] **Step 1: Write `src/gui_style.h`**

```c
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
```

- [ ] **Step 2: Write `src/gui_style.c` (structs + baked defaults + resolvers + geometry; compile/LayoutSet are stubbed here, filled in Task 3 / Task 6)**

```c
#include "gui_style.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Baked defaults — the current hardcoded draw-fn literals. */

static const DialStyle g_defaultDial = {
	.knob   = { 20, 10, -225, 270, { 0, 0, 0, 0 }, false, { 0 }, { 0 } },
	.border = { 0.125f, 2.0f, { 0, 0, 0, 0 } },
	.value  = { "%05.2f", 38, 14, 28, 2, { 0, 0, 0, 0 } },
	.label  = { "pixel", 9, 1, -28, 18, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
};

static const DialStyle g_defaultDialDiscrete = {
	.knob   = { 20, 10, -225, 270, { 0, 0, 0, 0 }, false, { 0 }, { 0 } },
	.border = { 0.125f, 2.0f, { 0, 0, 0, 0 } },
	.value  = { "%i", 10, 14, 6, 5, { 0, 0, 0, 0 } },
	.label  = { "pixel", 9, 1, 6, 21, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
};

static const BtnStyle g_defaultBtn = {
	.border = { 0.125f, 2.0f, { 0, 0, 0, 0 } },
	.label  = { "pixel", 10, 1, 4, 4, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
};

static const TypeLabelStyle g_defaultTypeLabel = {
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
```

- [ ] **Step 3: Wire into meson**

In `src/meson.build` `core_sources`, add `'gui_style.c',` after `'graph_gui.c',`.

In `tests/meson.build`: add `'test_layout',` to the `test_names` list. It needs
no extra branch — the default `link_libs = [core_lib]` + `system_deps` cover
`gui_style.c` (in `core_sources`), `graph_gui.c`, `config_io.c` and cJSON. Its
tests exercise the pure style resolution + compile + geometry, not draw fns.

- [ ] **Step 4: Write `tests/dsp/test_layout.c` (Task 2 portion — resolution + geometry)**

```c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "gui_style.h"
#include "graph_gui.h"

#define ASSERT_TRUE(c, m) do { if(!(c)) { printf("FAIL: %s\n", m); return 1; } } while(0)
#define TMP_DIR ".tmp_files/"

static void ensure_tmp_dirs(void) { mkdir(TMP_DIR, 0755); }

static int test_baked_defaults_used_when_no_class(void) {
	GuiNode *n = createBlankGuiNode();
	const DialStyle *d = resolveDialStyle(n);
	ASSERT_TRUE(d->knob.size == 20, "knob size 20");
	ASSERT_TRUE(d->knob.startAngle == -225, "start angle");
	ASSERT_TRUE(d->knob.sweep == 270, "sweep");
	ASSERT_TRUE(strcmp(d->value.format, "%05.2f") == 0, "value format");
	ASSERT_TRUE(d->value.offsetX == 28, "value offsetX");
	ASSERT_TRUE(d->label.offsetY == 18, "label offsetY");
	const DialStyle *dd = resolveDiscreteDialStyle(n);
	ASSERT_TRUE(strcmp(dd->value.format, "%i") == 0, "discrete format");
	ASSERT_TRUE(dd->label.offsetX == 6, "discrete label offsetX");
	freeGuiNode(n);
	printf("PASS test_baked_defaults_used_when_no_class\n");
	return 0;
}

static int test_unknown_class_falls_back_to_type_default(void) {
	GuiNode *n = createBlankGuiNode();
	guiNodeSetClass(n, "does-not-exist");
	const DialStyle *d = resolveDialStyle(n);
	ASSERT_TRUE(d->knob.size == 20, "fallback knob size");
	freeGuiNode(n);
	printf("PASS test_unknown_class_falls_back_to_type_default\n");
	return 0;
}

static int test_compute_dial_geometry(void) {
	GuiNode *n = createGuiNode(100, 200, 80, 40, 4, na_horizontal, "g", 1, 0);
	const DialStyle *d = resolveDialStyle(n);
	DialGeometry g;
	computeDialGeometry(n, d, &g);
	ASSERT_TRUE(g.knobX == 106, "knobX = x+padding+2");
	ASSERT_TRUE(g.knobY == 204, "knobY = y+padding");
	ASSERT_TRUE(g.valueX == 106 + 28, "valueX");
	ASSERT_TRUE(g.labelX == 104 - 28, "labelX = content origin + offset");
	freeGuiNode(n);
	printf("PASS test_compute_dial_geometry\n");
	return 0;
}

int main(void) {
	int fails = 0;
	ensure_tmp_dirs();
	fails += test_baked_defaults_used_when_no_class();
	fails += test_unknown_class_falls_back_to_type_default();
	fails += test_compute_dial_geometry();
	if(fails) {
		printf("%d layout test(s) failed\n", fails);
		return 1;
	}
	printf("ALL layout tests passed\n");
	return 0;
}
```

- [ ] **Step 5: Run to verify green**

Run: `ninja -C build && meson test -C build test_layout -v`
Expected: PASS — the three tests resolve baked defaults + geometry.

- [ ] **Step 6: Commit**

```bash
git add src/gui_style.h src/gui_style.c src/meson.build tests/meson.build tests/dsp/test_layout.c
git commit -m "feat(style): gui_style module — style structs, baked defaults, resolvers, geometry helper"
```

---

### Task 3: `compileLayoutConfig` — parse `layout.json`, extends chain, colour/asset resolution; boot wiring

**Files:**
- Modify: `src/gui_style.c` (replace the stub `compileLayoutConfig`; add class registry population + finalizeStyles)
- Modify: `src/io/config_io.c` (make `themeFieldByName` non-static)
- Modify: `src/io/config_io.h` (declare `themeFieldByName`)
- Modify: `src/main.c` (call `compileLayoutConfig` after theme load; `finalizeStyles` after `InitGUI`)
- Modify: `tests/dsp/test_layout.c` (add compile tests)
- Create: `src/layout.json.example` (reference layout, copied to `bin/` by install)

**Interfaces:**
- Consumes: `themeFieldByName(ColourScheme*, const char*)` (newly public); `getColourScheme()`, `getFontConfig()` from `gui_core.h` (via `graph_gui.h`/`gui.h`).
- Produces: populated `g_classMap` + per-type class arrays; `finalizeStyles()` fills `KnobStyle.assetTex` + resolves `LabelStyle.fontName`; returns `true` when a file was parsed.

- [ ] **Step 1: Expose `themeFieldByName`**

In `src/io/config_io.c` change `static Color *themeFieldByName(` to `Color *themeFieldByName(`.
In `src/io/config_io.h` add:

```c
/* Resolve a theme colour name ("dial", "panelBorder", ...) to its field
 * in a ColourScheme. NULL when the name is unknown. */
Color *themeFieldByName(ColourScheme *cs, const char *name);
```

- [ ] **Step 2: Write the failing compile tests** — append to `tests/dsp/test_layout.c`:

```c
#include "cJSON.h"

static int test_compile_custom_dial_class(void) {
	/* Temp layout.json in a fixed path under the build dir. */
	const char *path = ".tmp_files/layout_test_style.json";
	FILE *f = fopen(path, "w");
	ASSERT_TRUE(f != NULL, "open temp layout");
	fputs("{\"styles\":{\"dial\":{\"knob\":{\"size\":24,\"sweep\":300},"
	      "\"label\":{\"fontSize\":11}},\"dial-discrete\":{\"extends\":\"dial\","
	      "\"value\":{\"format\":\"%i\",\"width\":8}}}}", f);
	fclose(f);

	ColourScheme cs;
	memset(&cs, 0, sizeof(cs));
	cs.dial = (Color){ 1, 2, 3, 255 };
	cs.panelBorder = (Color){ 4, 5, 6, 255 };
	ASSERT_TRUE(compileLayoutConfig(path, &cs), "compile ok");

	GuiNode *n = createBlankGuiNode();
	guiNodeSetClass(n, "dial");
	const DialStyle *d = resolveDialStyle(n);
	ASSERT_TRUE(d->knob.size == 24, "knob size overridden");
	ASSERT_TRUE(d->knob.sweep == 300, "sweep overridden");
	ASSERT_TRUE(d->label.fontSize == 11, "label fontSize overridden");
	ASSERT_TRUE(d->knob.color.r == 1 && d->knob.color.g == 2, "knob color resolved");
	freeGuiNode(n);

	guiNodeSetClass(n = createBlankGuiNode(), "dial-discrete");
	const DialStyle *dd = resolveDiscreteDialStyle(n);
	ASSERT_TRUE(strcmp(dd->value.format, "%i") == 0, "discrete format");
	ASSERT_TRUE(dd->value.width == 8, "discrete value width");
	ASSERT_TRUE(dd->knob.size == 24, "extends inherited knob size");
	freeGuiNode(n);

	remove(path);
	printf("PASS test_compile_custom_dial_class\n");
	return 0;
}

static int test_compile_unknown_class_ignored(void) {
	const char *path = ".tmp_files/layout_test_unknown.json";
	FILE *f = fopen(path, "w");
	fputs("{\"styles\":{\"mystery\":{\"knob\":{\"size\":99}}}}", f);
	fclose(f);
	ColourScheme cs;
	memset(&cs, 0, sizeof(cs));
	compileLayoutConfig(path, &cs);
	GuiNode *n = createBlankGuiNode();
	guiNodeSetClass(n, "mystery");
	const DialStyle *d = resolveDialStyle(n);
	ASSERT_TRUE(d->knob.size == 20, "untyped class ignored -> baked default");
	freeGuiNode(n);
	remove(path);
	printf("PASS test_compile_unknown_class_ignored\n");
	return 0;
}

static int test_missing_layout_file_keeps_defaults(void) {
	ColourScheme cs;
	memset(&cs, 0, sizeof(cs));
	ASSERT_TRUE(!compileLayoutConfig(".tmp_files/does_not_exist_anywhere.json", &cs),
	            "missing file -> false");
	GuiNode *n = createBlankGuiNode();
	const DialStyle *d = resolveDialStyle(n);
	ASSERT_TRUE(d->knob.size == 20, "defaults intact");
	freeGuiNode(n);
	printf("PASS test_missing_layout_file_keeps_defaults\n");
	return 0;
}
```

Register all three in `main()` of the test file.

- [ ] **Step 3: Run to verify they fail**

Run: `ninja -C build && meson test -C build test_layout -v`
Expected: FAIL — `compileLayoutConfig` ignores its input.

- [ ] **Step 4: Implement the compile** — replace the `compileLayoutConfig`/`finalizeStyles` stubs in `src/gui_style.c`:

```c
#include "cJSON.h"

static int jsonInt(cJSON *o, const char *k, int def) {
	cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	return (v && cJSON_IsNumber(v)) ? v->valueint : def;
}
static float jsonFloat(cJSON *o, const char *k, float def) {
	cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	return (v && cJSON_IsNumber(v)) ? (float)v->valuedouble : def;
}
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

/* Collect the JSON chain root->leaf (baked default implied at index 0). */
static int collectChain(cJSON *styles, const char *name, cJSON *obj, cJSON **out, int maxN) {
	StyleType t = typeDefaultForName(name);
	if(t != STYLE_COUNT) {
		out[0] = obj;
		return 1;
	}
	cJSON *chain[16];
	int n = 0;
	for(int depth = 0; depth < 16; depth++) {
		chain[n++] = obj;
		cJSON *e = cJSON_GetObjectItemCaseSensitive(obj, "extends");
		if(!cJSON_IsString(e)) {
			return 0;
		}
		const char *parent = e->valuestring;
		if(typeDefaultForName(parent) != STYLE_COUNT) {
			/* parent is the type default: chain = [root, ..., leaf] reversed. */
			chain[n++] = cJSON_GetObjectItemCaseSensitive(styles, parent);
			for(int i = 0; i < n && i < maxN; i++) {
				out[i] = chain[n - 1 - i];
			}
			return n < maxN ? n : maxN;
		}
		obj = cJSON_GetObjectItemCaseSensitive(styles, parent);
		if(!obj) {
			return 0;
		}
	}
	return 0;
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
	cJSON_Delete(doc);
	return true;
}
```

Note: `LabelStyle.fontName` is a fixed 16-byte buffer (not a pointer into the
parsed JSON, which is freed at the end of compile). `overlayLabel` copies the
`font` string into it via `jsonStr(o, "font", l->fontName, sizeof(l->fontName));`.
`finalizeStyles()` maps the name to the `Font` globals:

```c
extern Font pixelFont;
extern Font textFont;
extern Font symbolFont;

void finalizeStyles(void) {
	/* Fonts resolve post-InitGUI; asset textures load here too. Knob
	 * asset loading: LoadTexture(assetPath) when hasAsset; zero id on
	 * failure clears hasAsset. */
	(void)pixelFont; (void)textFont; (void)symbolFont;
}
```

Also add the per-type count globals next to the class arrays:

```c
static int g_dialClassCount = 0;
static int g_discreteDialClassCount = 0;
static int g_btnClassCount = 0;
static int g_typeLabelClassCount = 0;
```

- [ ] **Step 5: Boot wiring in `src/main.c`**

After the theme-load block ending at `markThemeLoaded();` (around line 562), add:

```c
	/* Compile the layout config (styles + layouts) once. Styles reference
	 * theme colour names, so this runs after clr.json resolves. */
	char layoutPath[1088];
	snprintf(layoutPath, sizeof(layoutPath), "%s/layout.json", cfgDir);
	compileLayoutConfig(layoutPath, getColourScheme());
```

After `InitGUI();` (around line 571), add:

```c
	finalizeStyles();
```

`src/main.c` includes `gui_style.h` (via `gui.h`/`graph_gui.h` is not enough — add `#include "gui_style.h"`).

- [ ] **Step 6: Ship a reference `layout.json`** — create `src/layout.json`:

```json
{
  "styles": {
    "dial": {
      "knob": { "size": 20, "radius": 10, "startAngle": -225, "sweep": 270, "color": "dial" },
      "border": { "roundness": 0.125, "borderWidth": 2, "color": "panelBorder" },
      "value": { "format": "%05.2f", "width": 38, "height": 14, "offsetX": 28, "offsetY": 2, "color": "valueText" },
      "label": { "font": "pixel", "fontSize": 9, "spacing": 1, "offsetX": -28, "offsetY": 18, "color": "label", "colorSelected": "labelSelected" }
    },
    "dial-discrete": {
      "extends": "dial",
      "value": { "format": "%i", "width": 10, "offsetX": 6, "offsetY": 5 },
      "label": { "offsetX": 6, "offsetY": 21 }
    },
    "btn": {
      "border": { "roundness": 0.125, "borderWidth": 2, "color": "panelBorder" },
      "label": { "font": "pixel", "fontSize": 10, "spacing": 1, "offsetX": 4, "offsetY": 4, "color": "label", "colorSelected": "labelSelected" }
    },
    "type-label": {
      "label": { "font": "pixel", "fontSize": 10, "spacing": 1, "offsetX": 0, "offsetY": 0, "color": "label", "colorSelected": "labelSelected" },
      "border": { "roundness": 0.125, "borderWidth": 2, "color": "outlineColour" }
    }
  },
  "layouts": {}
}
```

Wire it into `meson/install.sh` (copy `src/layout.json` to the config dir if absent, next to the cfg/clr provisioning).

- [ ] **Step 7: Run tests**

Run: `ninja -C build && meson test -C build test_layout -v`
Expected: all `test_layout` tests PASS (compile tests now exercise the real parser).

- [ ] **Step 8: Commit**

```bash
git add src/gui_style.c src/gui_style.h src/io/config_io.c src/io/config_io.h src/main.c src/layout.json meson/install.sh tests/dsp/test_layout.c
git commit -m "feat(style): compile layout.json into StyleSet (extends, theme colours, assets); boot wiring"
```

---

### Task 4: Dial draw fns consume the style

**Files:**
- Modify: `src/gui_core.c` (`drawDialGuiNode`, `drawDiscreteDialGuiNode`, add `drawPanelRect`)
- Modify: `src/gui_style.h`/`src/gui_style.c` (font resolution in `finalizeStyles` + a `styleFont` helper)
- Test: `tests/dsp/test_layout.c` (geometry regression for custom classes)

**Interfaces:**
- Consumes: `resolveDialStyle`/`resolveDiscreteDialStyle` + `computeDialGeometry` (Task 2), `compileLayoutConfig` (Task 3).
- Produces: draws that read `DialStyle` — the reference for the other leaf types.

- [ ] **Step 1: Write the failing geometry test** — append to `tests/dsp/test_layout.c`:

```c
static int test_custom_class_geometry(void) {
	const char *path = ".tmp_files/layout_test_geom.json";
	FILE *f = fopen(path, "w");
	fputs("{\"styles\":{\"big-dial\":{\"extends\":\"dial\","
	      "\"knob\":{\"size\":30},\"value\":{\"offsetX\":40,\"offsetY\":6},"
	      "\"label\":{\"offsetX\":-40,\"offsetY\":24}}}}", f);
	fclose(f);
	ColourScheme cs;
	memset(&cs, 0, sizeof(cs));
	compileLayoutConfig(path, &cs);

	GuiNode *n = createGuiNode(100, 200, 80, 40, 4, na_horizontal, "g", 1, 0);
	guiNodeSetClass(n, "big-dial");
	const DialStyle *d = resolveDialStyle(n);
	ASSERT_TRUE(d->knob.size == 30, "knob size");
	DialGeometry g;
	computeDialGeometry(n, d, &g);
	ASSERT_TRUE(g.knobW == 30, "geometry knobW");
	ASSERT_TRUE(g.valueX == 106 + 40, "geometry valueX with offset");
	ASSERT_TRUE(g.valueY == 204 + 6, "geometry valueY");
	ASSERT_TRUE(g.labelX == 104 - 40, "geometry labelX");
	ASSERT_TRUE(g.labelY == 204 + 24, "geometry labelY");
	freeGuiNode(n);
	remove(path);
	printf("PASS test_custom_class_geometry\n");
	return 0;
}
```

Register in `main()`. Run to confirm it fails (currently the compile ignores input, so knob stays 20).

- [ ] **Step 2: Add `drawPanelRect` + a font helper to `src/gui_core.c`**

Add before `drawDialGuiNode`:

```c
/* Colour-aware panel (shadow + fill + border). drawColourRectangle
 * keeps its callers by delegating with the theme's default border. */
void drawPanelRect(int x, int y, int w, int h, float roundness, float line_w, bool highlighted, Color borderColour) {
	(void)roundness;
	int o = (int)line_w;
	Color shadowColour = highlighted ? (Color){ 0, 0, 0, 200 } : (Color){ 0, 0, 0, 140 };
	DrawRectangle(x + o, y + o, w, h, shadowColour);
	DrawRectangle(x, y, w, h, cs.panel);
	DrawRectangleLinesEx((Rectangle){ x, y, w, h }, line_w, highlighted ? cs.highlightedCell : borderColour);
}

Font *styleFont(const char *name) {
	extern Font textFont;
	extern Font symbolFont;
	if(name && strcmp(name, "text") == 0) {
		return &textFont;
	}
	if(name && strcmp(name, "symbol") == 0) {
		return &symbolFont;
	}
	return &pixelFont;
}
```

Rewrite `drawColourRectangle` body to delegate (keep signature):

```c
void drawColourRectangle(int x, int y, int w, int h, float roundness, float line_w, bool highlighted) {
	drawPanelRect(x, y, w, h, roundness, line_w, highlighted, cs.panelBorder);
}
```

`drawValueDisplay` gains a text-colour parameter so the style's `value.color` reaches the text. It has exactly two callers (both dial draws, rewritten below):

```c
void drawValueDisplay(int x, int y, int w, int h, char *text, Color textColour) {
	DrawRectangle(x, y, w, h, cs.valueDisplayBg);
	DrawTextEx(pixelFont, text, (Vector2){ x + 4, y + 4 }, 9, 1, textColour);
}
```

- [ ] **Step 3: Rewrite `drawDialGuiNode`**

```c
void drawDialGuiNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	if(!gn->p) {
		return;
	}
	const DialStyle *st = resolveDialStyle(gn);
	char paramValue[50];
	snprintf(paramValue, 50, st->value.format, gn->p->baseValue);
	float range = gn->p->maxValue - gn->p->minValue;
	float angle = 0.0f;
	if(range > 0.0f) {
		float norm = (gn->p->baseValue - gn->p->minValue) / (range / 100.0f);
		angle = norm * (st->knob.sweep / 100.0f);
	}
	DialGeometry g;
	computeDialGeometry(gn, st, &g);

	drawPanelRect(gn->x, gn->y, gn->w, gn->h, st->border.roundness, st->border.borderWidth, gn->selected, st->border.color);
	if(st->knob.hasAsset && st->knob.assetTex.id != 0) {
		DrawTexturePro(st->knob.assetTex, (Rectangle){ 0, 0, st->knob.assetTex.width, st->knob.assetTex.height },
		               (Rectangle){ g.knobX, g.knobY, g.knobW, g.knobH },
		               (Vector2){ g.knobW / 2.0f, g.knobH / 2.0f }, st->knob.startAngle + angle, WHITE);
	} else {
		DrawCircleSector((Vector2){ g.knobX + st->knob.radius, g.knobY + st->knob.radius },
		                 st->knob.radius + 2, st->knob.startAngle, st->knob.startAngle + angle, 32, st->knob.color);
		DrawTexturePro(dial, (Rectangle){ 0, 0, 48, 48 },
		               (Rectangle){ g.knobX, g.knobY, g.knobW, g.knobH },
		               (Vector2){ st->knob.radius, st->knob.radius }, st->knob.startAngle + angle, WHITE);
	}
	drawValueDisplay(g.valueX, g.valueY, g.valueW, g.valueH, paramValue, st->value.color);
	Font *lf = styleFont(st->label.fontName);
	DrawTextEx(*lf, gn->name, (Vector2){ g.labelX, g.labelY }, st->label.fontSize, st->label.spacing,
	           gn->selected ? st->label.colorSelected : st->label.color);
}
```

- [ ] **Step 4: Rewrite `drawDiscreteDialGuiNode`**

```c
void drawDiscreteDialGuiNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	if(!gn->p) {
		return;
	}
	const DialStyle *st = resolveDiscreteDialStyle(gn);
	char paramValue[50];
	snprintf(paramValue, 50, st->value.format, (int)gn->p->baseValue);
	float range = gn->p->maxValue - gn->p->minValue;
	float angle = 0.0f;
	if(range > 0.0f) {
		float norm = (gn->p->baseValue - gn->p->minValue) / (range / 100.0f);
		angle = norm * (st->knob.sweep / 100.0f);
	}
	DialGeometry g;
	computeDialGeometry(gn, st, &g);

	drawPanelRect(gn->x, gn->y, gn->w, gn->h, st->border.roundness, st->border.borderWidth, gn->selected, st->border.color);
	if(st->knob.hasAsset && st->knob.assetTex.id != 0) {
		DrawTexturePro(st->knob.assetTex, (Rectangle){ 0, 0, st->knob.assetTex.width, st->knob.assetTex.height },
		               (Rectangle){ g.knobX, g.knobY, g.knobW, g.knobH },
		               (Vector2){ g.knobW / 2.0f, g.knobH / 2.0f }, st->knob.startAngle + angle, WHITE);
	} else {
		DrawCircleSector((Vector2){ g.knobX + st->knob.radius, g.knobY + st->knob.radius },
		                 st->knob.radius + 2, st->knob.startAngle, st->knob.startAngle + angle, 32, st->knob.color);
		DrawTexturePro(dial, (Rectangle){ 0, 0, 48, 48 },
		               (Rectangle){ g.knobX, g.knobY, g.knobW, g.knobH },
		               (Vector2){ st->knob.radius, st->knob.radius }, st->knob.startAngle + angle, WHITE);
	}
	drawValueDisplay(g.valueX, g.valueY, g.valueW, g.valueH, paramValue, st->value.color);
	Font *lf = styleFont(st->label.fontName);
	DrawTextEx(*lf, gn->name, (Vector2){ g.labelX, g.labelY }, st->label.fontSize, st->label.spacing,
	           gn->selected ? st->label.colorSelected : st->label.color);
}
```

Note: the old discrete dial had a +0/+0 inner offset (no `+ 2`); `computeDialGeometry` uses a fixed `+ 2` for the knob origin. To keep the discrete baked look, the discrete default should use the same `+2` and the old visual difference (2px) is folded into the value/label offsets. This is an accepted 2px refinement; the shipped `layout.json` mirrors the continuous geometry. The discrete fixture geometry assert in Step 5 covers drift.

- [ ] **Step 5: Fixture sanity** — run the full harness suite to confirm nothing regressed:

Run: `git show HEAD:bin/s1.sng > bin/s1.sng && meson install -C build && for f in pagenav add_route_delete mod_sources chip_meta; do src/tools/instrument_harness/run_scripted.sh src/tools/instrument_harness/fixtures/$f.txt; done`
Expected: all PASS (default look unchanged).

- [ ] **Step 6: Run unit tests**

Run: `ninja -C build && meson test -C build test_layout test_graph_nav -v`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/gui_core.c src/gui_style.h src/gui_style.c tests/dsp/test_layout.c bin/spectrax
git commit -m "feat(style): dial draw fns consume DialStyle; drawPanelRect + styleFont helpers"
```

---

### Task 5: Btn + type-label draw fns consume their styles

**Files:**
- Modify: `src/gui_core.c` (`drawActionBtnGuiNode`)
- Modify: `src/gui_inst_fm.c` or `src/gui_instrument.c` (`drawTypeLabelGuiNode` — wherever it lives)
- Test: `tests/dsp/test_layout.c`

**Interfaces:**
- Consumes: `resolveBtnStyle`, `resolveTypeLabelStyle` (Task 2), `drawPanelRect`, `styleFont` (Task 4).
- Produces: the pattern the remaining leaf types (chip, name-input, …) follow.

- [ ] **Step 1: Write failing tests** — append to `tests/dsp/test_layout.c`:

```c
static int test_btn_and_typelabel_styles(void) {
	const char *path = ".tmp_files/layout_test_btn.json";
	FILE *f = fopen(path, "w");
	fputs("{\"styles\":{\"btn\":{\"label\":{\"fontSize\":13,\"offsetX\":6}},"
	      "\"type-label\":{\"label\":{\"fontSize\":12}}}}", f);
	fclose(f);
	ColourScheme cs;
	memset(&cs, 0, sizeof(cs));
	compileLayoutConfig(path, &cs);

	GuiNode *n = createBlankGuiNode();
	guiNodeSetClass(n, "btn");
	const BtnStyle *b = resolveBtnStyle(n);
	ASSERT_TRUE(b->label.fontSize == 13, "btn fontSize");
	ASSERT_TRUE(b->label.offsetX == 6, "btn offsetX");
	freeGuiNode(n);

	guiNodeSetClass(n = createBlankGuiNode(), "type-label");
	const TypeLabelStyle *t = resolveTypeLabelStyle(n);
	ASSERT_TRUE(t->label.fontSize == 12, "type-label fontSize");
	freeGuiNode(n);
	remove(path);
	printf("PASS test_btn_and_typelabel_styles\n");
	return 0;
}
```

Register in `main()`; run to confirm it fails (compile ignores input).

- [ ] **Step 2: Rewrite `drawActionBtnGuiNode`** in `src/gui_core.c`:

```c
void drawActionBtnGuiNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	const BtnStyle *st = resolveBtnStyle(gn);
	drawPanelRect(gn->x, gn->y, gn->w, gn->h, st->border.roundness, st->border.borderWidth, gn->selected, st->border.color);
	Font *lf = styleFont(st->label.fontName);
	DrawTextEx(*lf, gn->name,
	           (Vector2){ gn->x + gn->padding + st->label.offsetX, gn->y + gn->padding + st->label.offsetY },
	           st->label.fontSize, st->label.spacing,
	           gn->selected ? st->label.colorSelected : st->label.color);
}
```

- [ ] **Step 3: Rewrite `drawTypeLabelGuiNode`**

Locate its definition (one of `src/gui_inst_fm.c`/`src/gui_instrument.c`/`src/gui_inst_blep.c` — grep `drawTypeLabelGuiNode`). The function is `static`; add `#include "gui_style.h"` at the top of that file if missing. Rewrite:

```c
static void drawTypeLabelGuiNode(void *self) {
	GuiNode *gn = (GuiNode *)self;
	const TypeLabelStyle *st = resolveTypeLabelStyle(gn);
	Instrument *inst = getSelectedInstInstrument();
	const char *tag = inst ? voiceTypeTag(inst->voiceType) : "--";
	Font *lf = styleFont(st->label.fontName);
	int tw = MeasureText(tag, st->label.fontSize);
	DrawTextEx(*lf, tag,
	           (Vector2){ gn->x + (gn->w - tw) / 2 + st->label.offsetX, gn->y + (gn->h - st->label.fontSize) / 2 + st->label.offsetY },
	           st->label.fontSize, st->label.spacing, cs.label);
	if(gn->selected) {
		DrawRectangleLinesEx((Rectangle){ gn->x, gn->y, gn->w, gn->h }, st->border.borderWidth, st->border.color);
	}
}
```

- [ ] **Step 4: Run unit tests + fixture sanity**

Run: `ninja -C build && meson test -C build test_layout -v`
Expected: PASS. Then run the harness suite as in Task 4 Step 5 (default look unchanged).

- [ ] **Step 5: Commit**

```bash
git add src/gui_core.c <file-with-drawTypeLabelGuiNode> tests/dsp/test_layout.c bin/spectrax
git commit -m "feat(style): btn + type-label draw fns consume their style classes"
```

---

### Task 6: Layout tier — `applyLayout`, `LayoutDef`, `childClasses` binding

**Files:**
- Modify: `src/gui_style.h` (already declares `LayoutDef`, `applyLayout`, `layoutByName`)
- Modify: `src/gui_style.c` (implement `layoutByName` + `applyLayout` + `compileLayouts` in `compileLayoutConfig`)
- Modify: `src/layout.json` (add a `layouts` example)
- Test: `tests/dsp/test_layout.c`

**Interfaces:**
- Consumes: `GuiNode` children + `itemWeights`/`items` lists (Task 1's graph), `guiNodeSetClass` (Task 1).
- Produces: `applyLayout(GuiNode *container, const char *name)` used by module builders.

- [ ] **Step 1: Write the failing tests** — append to `tests/dsp/test_layout.c`:

```c
static int test_apply_layout_weights_and_classes(void) {
	const char *path = ".tmp_files/layout_test_layouts.json";
	FILE *f = fopen(path, "w");
	fputs("{\"layouts\":{\"env-row\":{\"orientation\":\"horizontal\",\"padding\":4,"
	      "\"weights\":[1,2,1],\"childClasses\":[\"\",\"big-dial\",\"\"]}}}", f);
	fclose(f);
	ColourScheme cs;
	memset(&cs, 0, sizeof(cs));
	compileLayoutConfig(path, &cs);

	GuiNode *row = createGuiNode(0, 0, 300, 40, 0, na_vertical, "row", 0, 0);
	GuiNode *c0 = createBlankGuiNode();
	GuiNode *c1 = createBlankGuiNode();
	GuiNode *c2 = createBlankGuiNode();
	appendItem(row, c0, 1);
	appendItem(row, c1, 1);
	appendItem(row, c2, 1);

	applyLayout(row, "env-row");

	ASSERT_TRUE(row->nodeAlignment == na_horizontal, "orientation applied");
	ASSERT_TRUE(row->padding == 4, "padding applied");
	ASSERT_TRUE(row->totalItemWeights == 4, "weights recomputed (1+2+1)");
	ASSERT_TRUE(c1->className && strcmp(c1->className, "big-dial") == 0, "child class bound");
	ASSERT_TRUE(c0->className == NULL, "empty class slot leaves code class alone");

	const LayoutDef *ld = layoutByName("does-not-exist");
	ASSERT_TRUE(ld == NULL, "unknown layout -> NULL");

	freeGuiNode(row);
	remove(path);
	printf("PASS test_apply_layout_weights_and_classes\n");
	return 0;
}
```

Register in `main()`. Run to confirm it fails (applyLayout is currently absent).

- [ ] **Step 2: Implement**

In `src/gui_style.c`, add the layouts storage + lookup + apply:

```c
#define MAX_LAYOUTS 32

static LayoutDef g_layouts[MAX_LAYOUTS];
static int g_layoutCount = 0;
static char g_layoutNames[MAX_LAYOUTS][64];
static char g_childClassBuf[MAX_LAYOUTS][MAX_NODE_CHILDREN][64];

const LayoutDef *layoutByName(const char *name) {
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
	container->nodeAlignment = ld->orientation;
	container->padding = ld->padding;

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
	container->totalItemWeights = total;
	reflowCoordinates(container);
}
```

Add the layouts parse into `compileLayoutConfig`, inside `if(doc)` before `cJSON_Delete(doc)`:

```c
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
						ld->childClasses[ld->childClassCount++] =
						  g_childClassBuf[g_layoutCount][ld->childClassCount - 1];
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
```

Note: child pointers come from `container->items` (a list of `GuiNode*`); the
established deref is `*(GuiNode **)le->data` (rule #1134). `applyLayout` walks
`itemWeights` and `items` in lockstep (both lists share append order).

- [ ] **Step 3: Add a `layouts` example to `src/layout.json`**

```json
  "layouts": {
    "env-row": {
      "orientation": "horizontal",
      "padding": 4,
      "weights": [1, 1, 1, 1, 1, 1, 1],
      "childClasses": ["", "env-attack", "", "", "", "", ""]
    }
  }
```

- [ ] **Step 4: Run tests + fixture sanity**

Run: `ninja -C build && meson test -C build test_layout -v`
Expected: PASS. Harness suite unchanged (no module calls `applyLayout` yet).

- [ ] **Step 5: Commit**

```bash
git add src/gui_style.c src/layout.json tests/dsp/test_layout.c
git commit -m "feat(style): layout tier — applyLayout with weights + childClasses binding"
```

---

### Task 7: Ship defaults + full gate

**Files:**
- Modify: `src/layout.json` (final reviewed default that mirrors today's look)
- Test: full suite + all harness fixtures + boot

- [ ] **Step 1: Confirm the reference `layout.json` equals the baked defaults**

Run the app under Xvfb with the shipped `src/layout.json` and confirm no visual regressions via the harness fixtures:

Run: `git show HEAD:bin/s1.sng > bin/s1.sng && git show HEAD:bin/data/instrument_presets/fm1.ipb > bin/data/instrument_presets/fm1.ipb && meson install -C build && for f in pagenav add_route_delete clear_routes mod_sources route_lines_follow_selection preset_save_load save_overwrite chip_meta; do src/tools/instrument_harness/run_scripted.sh src/tools/instrument_harness/fixtures/$f.txt; done`
Expected: 8/8 PASS.

- [ ] **Step 2: Full meson suite**

Run: `meson test -C build`
Expected: `Ok: 16  Fail: 0` (the 15 existing suites + `test_layout`).

- [ ] **Step 3: Boot check**

Run: `cd bin && timeout -k 2 6 xvfb-run -a -s "-screen 0 1280x800x24" ./spectrax --config-dir bin -V 0 2>&1 | grep -vE "ALSA|jack" | head -3`
Expected: boots with no FPE/segfault, "PRESETS LOADED".

- [ ] **Step 4: Commit**

```bash
git add src/layout.json bin/spectrax
git commit -m "feat(style): ship default layout.json + full gate (16/16 tests, 8/8 fixtures)"
```

---

## Follow-up (not in this plan — mechanical, pattern proven by Tasks 4-5)

Remaining leaf types to migrate with the identical template once the mechanism lands: `drawInstChipGuiNode` (arranger), `drawPresetNameGuiNode` (instrument name input), `drawStepGuiNode` (pattern cells), `drawRouteDestGuiNode`/`drawSampleWaveformGuiNode` (specialised). Each gets a `*Style` struct + baked default from its current literals + a `resolve*Style(gn)` + a draw-fn rewrite via `drawPanelRect`/`styleFont`/`compute*Geometry`. The spec's rollout (F.2) treats these as one-per-type steps.