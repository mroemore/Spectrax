# UI / Theme Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the approved UI/theme polish spec: a reusable text element, PTN-row parity, an envelope-stage rework with curve icons, cursor retention, a `--ui-preview` multi-screen window, and font/icon updates.

**Architecture:** All visual knobs are theme/layout-driven. New style types follow the existing `gui_style.c` pattern (baked default + class table + JSON overlay + `resolve*` + `compileLayoutConfig` switch). New drawable widgets follow the create/draw/destructor node conventions in `gui_core.c`, with every new `GuiNode` field zeroed in `initGuiNode` (rule #1154). Interactive behaviour is verified with the instrument harness fixtures; pure logic with the `tests/dsp` unit suite.

**Tech Stack:** C99, raylib 5.5 (vendored), cJSON, meson/ninja, PortAudio. Tests: `meson test -C build`; harness: `src/tools/instrument_harness`.

## Global Constraints

- No code comments unless asked.
- Every new `GuiNode` field must be zero-initialised in `initGuiNode` (`src/graph_gui.c`) — rule #1154.
- New style fields: baked default in `gui_style.c` must equal today's hardcoded literal; JSON overlay must be optional (missing keys keep the default).
- Theme/layout files: `bin/layout.json` (`styles` + `layouts`), `bin/clr.json`, `bin/cfg.json`.
- Run the full unit suite (`meson test -C build`) before every commit; it must stay green.
- Harness runs: `timeout -k 2 N xvfb-run -a <harness> <fixture>` then sweep `pkill -9 -x instrument_harness; pkill -9 -f "Xvfb :"` (rule #1137).
- Pre-existing failures are NOT regressions: `chip_meta.txt` (line 43), `save_overwrite.txt` (line 11); stray `bin/data/instrument_presets/Xm1.ipb`/`_m1.ipb` from preset fixtures must be deleted before `presetCount` asserts.
- No pushing to any remote without explicit user go-ahead (rule #1078).

---

## File Structure

- `src/graph_gui.h` / `src/graph_gui.c` — `GuiNode` struct, ctor/dtor, add `text` field + `guiNodeSetText`.
- `src/gui_style.h` / `src/gui_style.c` — `TextStyle`, `STYLE_TEXT`, `CurveIconStyle`, class tables, JSON overlay, compile switch, `styleFont`.
- `src/gui_core.c` — `createTextGuiNode`/`drawTextGuiNode`; button label honours `gn->text`; icon font load.
- `src/gui_internal.h` — draw-fn + factory declarations.
- `src/gui.h` — public factory declarations.
- `src/gui_inst_mod.c` — PTN row parity, envelope-stage rebuild, cursor reselect.
- `src/gui_instrument.c` — name-field font size.
- `src/main.c` — `--ui-preview` CLI plumbing + panel loop.
- `bin/layout.json`, `bin/cfg.json`, `bin/resources/fonts/pixelart-icons-font.ttf` (+ license).
- `tests/dsp/test_layout.c`, `tests/dsp/test_graph_nav.c` — unit tests.
- `src/tools/instrument_harness/fixtures/*.txt` — harness fixtures.

## Interfaces (names later tasks depend on)

- `void guiNodeSetText(GuiNode *gn, const char *text);`
- `void guiNodeSetSublabel(GuiNode *gn, const char *text);` (`char *sublabel` on `GuiNode`)
- `GuiNode *createTextGuiNode(int x,int y,int w,int h,const char *text,const char *className);`
- `void drawTextGuiNode(void *self);`
- `const TextStyle *resolveTextStyle(const GuiNode *gn);`
- `TextStyle` fields: `char fontName[16]; int fontSize, spacing, offsetX, offsetY, hAlign, vAlign; Color color, colorSelected;`
- Align enums: `TXT_ALIGN_LEFT=0, TXT_ALIGN_CENTER=1, TXT_ALIGN_RIGHT=2`, `TXT_ALIGN_MIDDLE=1, TXT_ALIGN_BOTTOM=2` (reuse the same ints; left/top = 0).
- `BtnStyle` gains `LabelStyle sublabel;`
- `ValueStyle` gains `int fontSize;`
- `const CurveIconStyle *resolveCurveIconStyle(const GuiNode *gn);`
- `GuiNode *findSourceTypeNode(GuiNode *root, Instrument *inst, int idx);`
- `const char *iconUtf8(const char *name);` (pixelarticons glyph, UTF-8)
- `bool uiPreviewRequested(int argc, char **argv); int uiPreviewFrames(int argc, char **argv);`

---

## Phase A — Generic text element + PTN row parity

### Task 1: `GuiNode.text` + `guiNodeSetText`

**Files:**
- Modify: `src/graph_gui.h:49` (struct field), `:102` (declaration)
- Modify: `src/graph_gui.c` (`initGuiNode`, `freeGuiNode`, new `guiNodeSetText`)
- Test: `tests/dsp/test_graph_nav.c`

**Interfaces:**
- Produces: `char *text` on `GuiNode`; `void guiNodeSetText(GuiNode *, const char *);`

- [ ] **Step 1: Write the failing test** (append to `tests/dsp/test_graph_nav.c`, register in `main`)

```c
static int test_guinode_text_lifecycle(void) {
    GuiNode *n = createGuiNode(0, 0, 40, 12, 0, na_horizontal, "IDENT", 0, 0);
    if(!n) { printf("FAIL alloc\n"); return 1; }
    if(n->text != NULL) { printf("FAIL text not zero-inited\n"); return 1; }
    guiNodeSetText(n, "hello");
    if(!n->text || strcmp(n->text, "hello") != 0) { printf("FAIL set\n"); return 1; }
    if(strcmp(n->name, "IDENT") != 0) { printf("FAIL name clobbered\n"); return 1; }
    guiNodeSetText(n, "world");
    if(strcmp(n->text, "world") != 0) { printf("FAIL reset\n"); return 1; }
    guiNodeSetText(n, NULL);
    if(n->text != NULL) { printf("FAIL clear\n"); return 1; }
    freeGuiNode(n);
    printf("PASS test_guinode_text_lifecycle\n");
    return 0;
}
```

- [ ] **Step 2: Run to verify it fails**
  `meson test -C build test_graph_nav` → compile error: `guiNodeSetText` undeclared / `n->text` unknown.

- [ ] **Step 3: Implement**
  In `src/graph_gui.h` add to `struct GuiNode` (next to `char *className;`):
  ```c
  char *text;
  ```
  and declaration after `void guiNodeSetClass(...)`:
  ```c
  void guiNodeSetText(GuiNode *gn, const char *text);
  ```
  In `src/graph_gui.c`: in `initGuiNode` set `gn->text = NULL;`. In `freeGuiNode`, before `free(gn)` add `free(gn->text);`. Add:
  ```c
  void guiNodeSetText(GuiNode *gn, const char *text) {
      if(!gn) {
          return;
      }
      free(gn->text);
      gn->text = text ? strdup(text) : NULL;
  }
  ```

- [ ] **Step 4: Run to verify it passes**
  `meson test -C build test_graph_nav` → PASS.

- [ ] **Step 5: Commit**
  `git add src/graph_gui.h src/graph_gui.c tests/dsp/test_graph_nav.c && git commit -m "feat(gui): GuiNode.text + guiNodeSetText"`

---

### Task 2: `TextStyle` + `STYLE_TEXT`

**Files:**
- Modify: `src/gui_style.h` (struct, enum, resolver decl), `src/gui_style.c` (default, class table, resolve, overlay, compile, default-name, colour resolve)
- Modify: `bin/layout.json` (add `"text"` style)
- Test: `tests/dsp/test_layout.c`

**Interfaces:**
- Produces: `TextStyle`, `STYLE_TEXT`, `resolveTextStyle`.

- [ ] **Step 1: Write the failing test**

```c
static int test_text_style_resolves(void) {
    GuiNode *n = createGuiNode(0, 0, 40, 12, 0, na_horizontal, "T", 0, 0);
    guiNodeSetClass(n, "text");
    const TextStyle *st = resolveTextStyle(n);
    if(!st) { printf("FAIL null\n"); return 1; }
    if(st->fontSize <= 0) { printf("FAIL fontSize %d\n", st->fontSize); return 1; }
    guiNodeSetClass(n, "no-such-class");
    const TextStyle *fallback = resolveTextStyle(n);
    if(!fallback) { printf("FAIL fallback null\n"); return 1; }
    freeGuiNode(n);
    printf("PASS test_text_style_resolves\n");
    return 0;
}
```

- [ ] **Step 2: Run to verify it fails** → `resolveTextStyle` undeclared.

- [ ] **Step 3: Implement**
  `src/gui_style.h` — add enum values `TXT_ALIGN_LEFT=0, TXT_ALIGN_CENTER=1, TXT_ALIGN_RIGHT=2, TXT_ALIGN_TOP=0, TXT_ALIGN_MIDDLE=1, TXT_ALIGN_BOTTOM=2`; struct:
  ```c
  typedef struct {
      char fontName[16];
      int fontSize;
      int spacing;
      int offsetX;
      int offsetY;
      int hAlign;
      int vAlign;
      Color color;
      Color colorSelected;
  } TextStyle;
  ```
  Add `STYLE_TEXT,` to `StyleType` before `STYLE_COUNT`; declare `const TextStyle *resolveTextStyle(const GuiNode *gn);`.

  `src/gui_style.c`:
  - default:
    ```c
    static TextStyle g_defaultText = {
        "pixel", 9, 0, 0, 0, TXT_ALIGN_LEFT, TXT_ALIGN_TOP, {0,0,0,0}, {0,0,0,0}
    };
    ```
  - `static TextStyle g_textClasses[MAX_CLASS_ENTRIES_PER_TYPE]; static int g_textClassCount = 0;`
  - `resolveDefaultColours`: `g_defaultText.color = cs->fontColour; g_defaultText.colorSelected = cs->fontColour;`
  - `g_defaultClassName`: `case STYLE_TEXT: return "text";`
  - `resolveTextStyle`: mirror `resolveBtnStyle` with `STYLE_TEXT`/`g_textClasses`/`g_defaultText`.
  - `overlayText`: 
    ```c
    static void overlayText(cJSON *o, const ColourScheme *cs, TextStyle *t) {
        jsonStr(o, "font", t->fontName, sizeof(t->fontName));
        t->fontSize = jsonInt(o, "fontSize", t->fontSize);
        t->spacing = jsonInt(o, "spacing", t->spacing);
        t->offsetX = jsonInt(o, "offsetX", t->offsetX);
        t->offsetY = jsonInt(o, "offsetY", t->offsetY);
        cJSON *ha = cJSON_GetObjectItemCaseSensitive(o, "hAlign");
        if(cJSON_IsString(ha) && ha->valuestring) {
            if(strcmp(ha->valuestring, "center") == 0) t->hAlign = TXT_ALIGN_CENTER;
            else if(strcmp(ha->valuestring, "right") == 0) t->hAlign = TXT_ALIGN_RIGHT;
            else t->hAlign = TXT_ALIGN_LEFT;
        }
        cJSON *va = cJSON_GetObjectItemCaseSensitive(o, "vAlign");
        if(cJSON_IsString(va) && va->valuestring) {
            if(strcmp(va->valuestring, "middle") == 0) t->vAlign = TXT_ALIGN_MIDDLE;
            else if(strcmp(va->valuestring, "bottom") == 0) t->vAlign = TXT_ALIGN_BOTTOM;
            else t->vAlign = TXT_ALIGN_TOP;
        }
        jsonColor(o, "color", cs, &t->color);
        jsonColor(o, "colorSelected", cs, &t->colorSelected);
    }
    ```
  - `compileLayoutConfig` switch: add `case STYLE_TEXT: { TextStyle merged = g_defaultText; for(...) overlayText(chain[i], cs, &merged); ...register STYLE_TEXT...; break; }` mirroring the `STYLE_DEST` block.
  - `finalizeStyles`: `(void)` any new font reference (add `g_defaultText` to the `(void)` line).
  - `bin/layout.json` `styles`: add
    ```json
    "text": { "font": "pixel", "fontSize": 9, "spacing": 0, "hAlign": "left", "vAlign": "top", "color": "font" }
    ```
    (use the same colour-name key the other styles use for text; if the key is `label`/`font`, mirror `g_defaultText.color`'s theme field name.)

- [ ] **Step 4: Run to verify it passes** → PASS.

- [ ] **Step 5: Commit**
  `git add src/gui_style.h src/gui_style.c bin/layout.json tests/dsp/test_layout.c && git commit -m "feat(style): TextStyle + STYLE_TEXT"`

---

### Task 3: `createTextGuiNode` / `drawTextGuiNode` + button label honours `text`

**Files:**
- Modify: `src/gui_core.c` (create/draw; `drawActionBtnGuiNode`)
- Modify: `src/gui_internal.h`, `src/gui.h`
- Test: `tests/dsp/test_graph_nav.c`

- [ ] **Step 1: Write the failing test**

```c
static int test_text_node_ctor(void) {
    GuiNode *n = createTextGuiNode(5, 6, 30, 10, "abc", "text");
    if(!n) { printf("FAIL alloc\n"); return 1; }
    if(n->selectable) { printf("FAIL selectable\n"); return 1; }
    if(!n->drawable || n->draw != drawTextGuiNode) { printf("FAIL draw wiring\n"); return 1; }
    if(!n->text || strcmp(n->text, "abc") != 0) { printf("FAIL text\n"); return 1; }
    if(n->x != 5 || n->y != 6) { printf("FAIL pos\n"); return 1; }
    freeGuiNode(n);
    printf("PASS test_text_node_ctor\n");
    return 0;
}
```

- [ ] **Step 2: Run to verify it fails** → `createTextGuiNode` undeclared.

- [ ] **Step 3: Implement**
  `src/gui_internal.h`: `void drawTextGuiNode(void *self);`
  `src/gui.h`: `GuiNode *createTextGuiNode(int x, int y, int w, int h, const char *text, const char *className);`
  `src/gui_core.c`:
  ```c
  void drawTextGuiNode(void *self) {
      GuiNode *gn = (GuiNode *)self;
      const TextStyle *st = resolveTextStyle(gn);
      Font *f = styleFont(st->fontName);
      const char *txt = gn->text ? gn->text : (gn->name ? gn->name : "");
      if(txt[0] == '\0' || st->fontSize <= 0) {
          return;
      }
      Vector2 size = MeasureTextEx(*f, txt, (float)st->fontSize, (float)st->spacing);
      float x = (float)gn->x + st->offsetX;
      if(st->hAlign == TXT_ALIGN_CENTER) { x = gn->x + (gn->w - size.x) * 0.5f + st->offsetX; }
      else if(st->hAlign == TXT_ALIGN_RIGHT) { x = gn->x + gn->w - size.x + st->offsetX; }
      float y = (float)gn->y + st->offsetY;
      if(st->vAlign == TXT_ALIGN_MIDDLE) { y = gn->y + (gn->h - size.y) * 0.5f + st->offsetY; }
      else if(st->vAlign == TXT_ALIGN_BOTTOM) { y = gn->y + gn->h - size.y + st->offsetY; }
      Color c = gn->selected ? st->colorSelected : st->color;
      DrawTextEx(*f, (char *)txt, (Vector2){ x, y }, (float)st->fontSize, (float)st->spacing, c);
  }

  GuiNode *createTextGuiNode(int x, int y, int w, int h, const char *text, const char *className) {
      GuiNode *gn = createGuiNode(x, y, w, h, 0, na_horizontal, "TEXT", 0, 0);
      if(!gn) {
          return NULL;
      }
      gn->drawable = true;
      gn->draw = drawTextGuiNode;
      if(className) {
          guiNodeSetClass(gn, className);
      }
      if(text) {
          guiNodeSetText(gn, text);
      }
      return gn;
  }
  ```
  In `drawActionBtnGuiNode` change the label source to `const char *label = gn->text ? gn->text : gn->name;` (use `label` where `gn->name` was measured/drawn).

- [ ] **Step 4: Run to verify it passes** → PASS (`meson test -C build test_graph_nav`).

- [ ] **Step 5: Commit**
  `git add src/gui_core.c src/gui_internal.h src/gui.h tests/dsp/test_graph_nav.c && git commit -m "feat(gui): createTextGuiNode/drawTextGuiNode; buttons honour text"`

---

### Task 4: `BtnStyle.sublabel` + `ValueStyle.fontSize`

**Files:**
- Modify: `src/gui_style.h` (structs), `src/gui_style.c` (overlay), `src/gui_core.c` (`drawActionBtnGuiNode`, `drawDialGuiNode` value draw)
- Test: `tests/dsp/test_layout.c`

- [ ] **Step 1: Write the failing test**

```c
static int test_btn_sublabel_resolves(void) {
    GuiNode *n = createGuiNode(0, 0, 40, 20, 0, na_horizontal, "B", 0, 0);
    guiNodeSetClass(n, "btn");
    const BtnStyle *st = resolveBtnStyle(n);
    if(!st) { printf("FAIL null\n"); return 1; }
    (void)st->sublabel;
    freeGuiNode(n);
    printf("PASS test_btn_sublabel_resolves\n");
    return 0;
}
```

- [ ] **Step 2: Run to verify it fails** → `sublabel` unknown.

- [ ] **Step 3: Implement**
  `gui_style.h`: add `int fontSize;` to `ValueStyle`; add `LabelStyle sublabel;` to `BtnStyle`.
  `gui_style.c`: in `g_defaultBtn` init `.sublabel = { "pixel", 8, 0, 4, 12, {0,0,0,0}, {0,0,0,0} }`; in `resolveDefaultColours` set `g_defaultBtn.sublabel.color = cs->label; g_defaultBtn.sublabel.colorSelected = cs->labelSelected;`; in `overlayValue` add `v->fontSize = jsonInt(o, "fontSize", v->fontSize);`; in `overlayBtn` add `cJSON *sl = cJSON_GetObjectItemCaseSensitive(o, "sublabel"); if(cJSON_IsObject(sl)) overlayLabel(sl, cs, &b->sublabel);`
  `gui_core.c`: in `drawActionBtnGuiNode`, after drawing the main label, if `st->sublabel.fontSize > 0` draw `gn->sublabel` text (a second `char *sublabel` field added to `GuiNode` the same way as `text`, or concatenate). **Decision: add `char *sublabel` to `GuiNode` too** (same ctor/dtor/setter pattern: `guiNodeSetSublabel`). Draw it centred using `st->sublabel`. In `drawDialGuiNode`/`drawValueDisplay`, pass `g.value.fontSize > 0 ? g.value.fontSize : 9` to `DrawTextEx` instead of the hardcoded `9`.

- [ ] **Step 4: Run to verify it passes** → PASS.

- [ ] **Step 5: Commit**
  `git add src/gui_style.h src/gui_style.c src/gui_core.c src/graph_gui.h src/graph_gui.c tests/dsp/test_layout.c && git commit -m "feat(style): BtnStyle.sublabel + ValueStyle.fontSize"`

---

### Task 5: PTN row parity

**Files:**
- Modify: `src/gui_inst_mod.c` (`appendPatternTrackRow` ~1962-1993; remove `drawPatternReadoutNode` ~1844-1871 and its use)
- Test: `src/tools/instrument_harness/fixtures/pattern_screen.txt`

- [ ] **Step 1: Rewrite `appendPatternTrackRow`**

Replace the body with (keeps the same `mod-source-row` wrapper, drops the readout, gives each button unique name + `ROUTE` text + sublabel stats):
```c
void appendPatternTrackRow(GuiNode *container, Instrument *inst, int weight) {
    if(!inst || !inst->modList) {
        return;
    }
    int firstPat = inst->coreEnvelopeCount - PATTERN_TRACKS;
    if(firstPat < 0 || modIndexAt(inst->modList, firstPat) < 0) {
        return;
    }
    GuiNode *wrap = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "MODSRC", 0, 0);
    wrap->drawable = true;
    wrap->draw = drawWrapperNode;
    GuiNode *label = createGuiNode(0, 0, 100, 100, 2, na_horizontal, "PTN", 0, 0);
    appendItem(wrap, label, 2);
    for(int t = 0; t < PATTERN_TRACKS; t++) {
        char tag[16];
        snprintf(tag, sizeof(tag), "PTN_ROUTE_%d", t + 1);
        GuiNode *route = createActionBtnGuiNode(0, 0, 100, 100, 2, na_horizontal,
                                                "ROUTE", 0, cbOpenRouteLayer, &g_sourceCtx[firstPat + t]);
        route->name = strdup(tag);
        guiNodeSetText(route, "ROUTE");
        char sub[24];
        int mi = modIndexAt(inst->modList, firstPat + t);
        Mod *m = (mi >= 0) ? inst->modList->mods[mi] : NULL;
        if(m && m->type == MT_PATTERN) {
            PatternState *p = &m->data.pattern;
            int len = p->length ? getParameterValueAsInt(p->length) : p->stepCount;
            int shape = p->shape ? getParameterValueAsInt(p->shape) : SH_HOLD;
            snprintf(sub, sizeof(sub), "L%d %s", len, patternShapeAbbrev(shape));
        } else {
            snprintf(sub, sizeof(sub), "--");
        }
        guiNodeSetSublabel(route, sub);
        appendItem(wrap, route, 3);
    }
    applyLayout(wrap, "mod-source-row");
    appendItem(container, wrap, weight);
}
```
Delete `drawPatternReadoutNode` and any reference.

- [ ] **Step 2: Build** → `meson compile -C build` clean.

- [ ] **Step 3: Update the fixture** — in `pattern_screen.txt` add a `JUMP PTN_ROUTE_1` + `ASSERT selected==ROUTE` (or the harness's SHOWTL/JUMPTL) block, and update `pagenav.txt` if it asserted `PTN_READ` / numeric button names.

- [ ] **Step 4: Run harness** → `pattern_screen` PASS; `pagenav` PASS.

- [ ] **Step 5: Commit**
  `git add src/gui_inst_mod.c src/tools/instrument_harness/fixtures/pattern_screen.txt src/tools/instrument_harness/fixtures/pagenav.txt && git commit -m "feat(mod): PTN row parity with text/sublabel route buttons"`

---

## Phase B — Envelope stage rework

### Task 6: Curve-icon atlas generation

**Files:**
- Create: `src/curve_icons.h`, `src/curve_icons.c` (or add to `gui_core.c` if a new module is undesirable — prefer a small module)
- Modify: `src/meson.build` (add source)
- Test: `tests/dsp/test_layout.c`

**Interfaces:**
- Produces: `#define CURVE_ICON_COUNT 32`, `#define CURVE_ICON_SIZE 8`, `int curveIconIndex(float curvature);` and `Texture2D buildCurveIconTexture(void);` (GL; init-only).

- [ ] **Step 1: Write the failing test**

```c
static int test_curve_icon_index(void) {
    if(curveIconIndex(0.0f) != 0) { printf("FAIL lo\n"); return 1; }
    if(curveIconIndex(1.0f) != CURVE_ICON_COUNT - 1) { printf("FAIL hi\n"); return 1; }
    if(curveIconIndex(0.5f) != 15 && curveIconIndex(0.5f) != 16) { printf("FAIL mid %d\n", curveIconIndex(0.5f)); return 1; }
    printf("PASS test_curve_icon_index\n");
    return 0;
}
```

- [ ] **Step 2: Run to verify it fails** → undeclared.

- [ ] **Step 3: Implement**
  `curve_icons.h`: constants + `int curveIconIndex(float curvature);` + `Texture2D buildCurveIconTexture(void);`
  `curve_icons.c`: `curveIconIndex` = clamp `(int)lroundf(curvature * (CURVE_ICON_COUNT - 1))` to `[0, 31]`. `buildCurveIconTexture` builds an `Image` of width `CURVE_ICON_COUNT * CURVE_ICON_SIZE` × `CURVE_ICON_SIZE`, for each frame uses a per-frame `float samples[8]` from `generateCurve(samples, 8, frame/(COUNT-1), 1)` and sets pixel `(fx, y)` where `y == (int)(samples[x] * (CURVE_ICON_SIZE-1))`, colour `WHITE` on transparent; `LoadTextureFromImage`, `UnloadImage`, `SetTextureFilter(tex, TEXTURE_FILTER_POINT)`. Add `curve_icons.c` to `src/meson.build`.
  Note: `generateCurve(float *data, size_t length, float curve, int steepnessFactor)` (modsystem.c:26).

- [ ] **Step 4: Run to verify it passes** → PASS.

- [ ] **Step 5: Commit**
  `git add src/curve_icons.h src/curve_icons.c src/meson.build tests/dsp/test_layout.c && git commit -m "feat(gui): 32-frame 8x8 curve-icon generator"`

---

### Task 7: `env-stage` layout + dial classes + `CurveIconStyle`

**Files:**
- Modify: `src/gui_style.h`, `src/gui_style.c`, `bin/layout.json`, `src/gui_core.c` (dial draw honours curve-icon)

**Interfaces:**
- Produces: `CurveIconStyle`, `resolveCurveIconStyle`; classes `stage-rate`, `stage-curve`; layout `env-stage`.

- [ ] **Step 1: Write the failing test**

```c
static int test_curve_icon_style(void) {
    GuiNode *n = createGuiNode(0, 0, 40, 20, 0, na_horizontal, "D", 0, 0);
    guiNodeSetClass(n, "stage-curve");
    const CurveIconStyle *st = resolveCurveIconStyle(n);
    if(!st) { printf("FAIL null\n"); return 1; }
    if(st->frameW != CURVE_ICON_SIZE) { printf("FAIL frameW %d\n", st->frameW); return 1; }
    freeGuiNode(n);
    printf("PASS test_curve_icon_style\n");
    return 0;
}
```

- [ ] **Step 2: Run to verify it fails** → undeclared.

- [ ] **Step 3: Implement**
  `gui_style.h`:
  ```c
  typedef struct {
      int frameW;
      int frameH;
      int frameCount;
      int offsetX;
      int offsetY;
      Color color;
      Color colorSelected;
  } CurveIconStyle;
  ```
  Add `STYLE_CURVE_ICON` to `StyleType`; `const CurveIconStyle *resolveCurveIconStyle(const GuiNode *gn);`
  `gui_style.c`: default `{CURVE_ICON_SIZE, CURVE_ICON_SIZE, CURVE_ICON_COUNT, 0, 0, {0,0,0,0}, {0,0,0,0}}`; class table + count; `resolveCurveIconStyle`; `overlayCurveIcon` (jsonInt frameW/frameH/frameCount/offsetX/offsetY + jsonColor color/colorSelected); `g_defaultClassName` `case STYLE_CURVE_ICON: return "curve-icon";`; compile switch case; `resolveDefaultColours` sets colors.
  Add per-class DialStyle overrides: `stage-rate` (value beneath: `value.offsetY` larger, `value.fontSize` smaller) and `stage-curve` (`knob.size` ≈ 2/3, `value.width=0, height=0, fontSize=0` to hide). Provide them as `"stage-rate": {"extends":"dial", ...}` / `"stage-curve": {"extends":"dial", ...}` in `bin/layout.json`, each also declaring a `"curveIcon"` sub-object if the dial draw reads it via the class (see below).
  `gui_core.c` `drawDialGuiNode`: when the resolved `CurveIconStyle` for `gn` has `frameCount > 0`, draw the curve-icon frame (from `curveIconsTexture`) at the knob centre for `curveIconIndex(getParameterValue(gn->p))` instead of the value text. The texture is a global built in `InitGUI` via `buildCurveIconTexture()`.
  `bin/layout.json` `layouts`: add
  ```json
  "env-stage": { "orientation": "horizontal", "padding": 2, "gap": 2, "weights": [2, 1], "childClasses": ["stage-rate", "stage-curve"] }
  ```

- [ ] **Step 4: Run to verify it passes** → PASS.

- [ ] **Step 5: Commit**
  `git add src/gui_style.h src/gui_style.c src/gui_core.c bin/layout.json tests/dsp/test_layout.c && git commit -m "feat(style): env-stage layout, stage-rate/stage-curve dial classes, curve-icon style"`

---

### Task 8: Rebuild the envelope stage UI

**Files:**
- Modify: `src/gui_inst_mod.c` (MT_ENV case ~1917-1924, and wherever env stages are built)
- Test: `src/tools/instrument_harness/fixtures/env_stage.txt` (new)

- [ ] **Step 1: Write the fixture** (discover names/geometry by running)
  Create `env_stage.txt`: enter instrument screen, `SCENE 1`, `JUMP ATTACK` (rate), assert selected; arrow to the curve dial, assert selected; `EDIT` + up, assert the curve value changed.
  Append to the harness fixture runner.

- [ ] **Step 2: Run to verify it fails** → selected name/geometry mismatch (stage box not present).

- [ ] **Step 3: Implement**
  Replace the `MT_ENV` branch with a stage-box builder that appends, per stage, a container with `applyLayout(stageWrap, "env-stage")`, containing: rate dial (`createDialGuiNode(..., "ATK", ...)`, class `stage-rate`), curve dial (`createDialGuiNode(..., "CRV", ...)`, class `stage-curve`, wired to `stages[k].curvature`), a `createTextGuiNode` `CRV` above the curve dial, and a `createTextGuiNode` `ATK`/`DEC` at top-right. The two `CURVE` dials for stages 0/1 become the curve dials for attack/decay respectively; the `ATTACK`/`DECAY` dials become the rate dials.
  Text nodes are siblings inside the stage container so their x/y come from reflow; use `hAlign`/`vAlign` in their classes for top-right placement.
  Add `"env-atk-tag"`, `"env-dec-tag"`, `"env-crv-tag"` text classes to `bin/layout.json` as needed (fontName pixel, small size, hAlign right/center).

- [ ] **Step 4: Run harness** → `env_stage` PASS.

- [ ] **Step 5: Commit**
  `git add src/gui_inst_mod.c src/tools/instrument_harness/fixtures/env_stage.txt && git commit -m "feat(mod): envelope stage box with curve-icon dial and ATK/DEC/CRV labels"`

---

## Phase C — Cursor retention

### Task 9: `findSourceTypeNode` + reselect on type cycle

**Files:**
- Modify: `src/gui_inst_mod.c` (`findSourceTypeNode`, `cbCycleSourceType`)
- Test: `src/tools/instrument_harness/fixtures/cursor_type_cycle.txt` (new)

- [ ] **Step 1: Write the fixture**
  Enter instrument, add a modulator, `JUMP` its type button (`ENV`), `EDIT` to cycle to `LFO`, then `ASSERT selected==LFO`.

- [ ] **Step 2: Run to verify it fails** → `selected==RATIO1` (current warp).

- [ ] **Step 3: Implement**
  Add a recursive helper:
  ```c
  GuiNode *findSourceTypeNode(GuiNode *root, Instrument *inst, int idx) {
      if(!root) {
          return NULL;
      }
      if(root->actionCb == cbCycleSourceType && root->actionCtx == &g_sourceCtx[idx]) {
          return root;
      }
      if(root->items) {
          ListElement *cur = root->items->head;
          for(int i = 0; i < root->itemCount && cur; i++, cur = cur->next) {
              GuiNode *hit = findSourceTypeNode(*(GuiNode **)cur->data, inst, idx);
              if(hit) {
                  return hit;
              }
          }
      }
      return NULL;
  }
  ```
  In `cbCycleSourceType`, capture `int idx = sc->idx;` before the rebuild; after `rebuildInstrumentGraph()` add:
  ```c
  GuiNode *keep = findSourceTypeNode(instrumentGraph->root, sc->inst, idx);
  if(keep) {
      changeGraphSelection(instrumentGraph, keep);
  }
  ```
  (Use the actual global graph name used by `rebuildInstrumentGraph`.)

- [ ] **Step 4: Run harness** → `cursor_type_cycle` PASS.

- [ ] **Step 5: Commit**
  `git add src/gui_inst_mod.c src/tools/instrument_harness/fixtures/cursor_type_cycle.txt && git commit -m "fix(mod): keep cursor on type-cycle button after rebuild"`

---

### Task 10: Reselect `MODS_ADD` after add

**Files:**
- Modify: `src/gui_inst_mod.c` (`cbAddModSource`)
- Test: `src/tools/instrument_harness/fixtures/cursor_add.txt` (new)

- [ ] **Step 1: Write the fixture** — `JUMP MODS_ADD`, `EDIT` (add), `ASSERT selected==MODS_ADD`.
- [ ] **Step 2: Run to verify it fails** → selected warps to top.
- [ ] **Step 3: Implement** — in `cbAddModSource`, after `addRuntimeSource(inst)` (which rebuilds), find the add button by name in the current instrument graph and `changeGraphSelection` to it, guarded by non-NULL.
- [ ] **Step 4: Run harness** → PASS.
- [ ] **Step 5: Commit** `git commit -m "fix(mod): keep cursor on add-modulator button after add"`

---

## Phase D — `--ui-preview`

### Task 11: CLI plumbing + panel-mode skeleton

**Files:**
- Modify: `src/main.c` (argv scan ~520, boot branch ~633-1056)
- Test: `src/tools/ui_preview_smoke.sh` (new) or a harness fixture

- [ ] **Step 1: Write the test** — a shell smoke: `timeout -k 2 20 xvfb-run -a ./bin/spectrax --ui-preview --ui-preview-frames 60`; assert exit 0.
- [ ] **Step 2: Run to verify it fails** → flag ignored.
- [ ] **Step 3: Implement**
  Add `bool uiPreviewRequested(int argc, char **argv)` and `int uiPreviewFrames(int argc, char **argv)` (mirroring the `--probe-route` scan; `--ui-preview-frames N` parses the following int, default 0 = run until closed). In `main`, if requested and `frames > 0`, render exactly N frames then exit 0. Skeleton: create a window, a grid of `createPresentTarget()`-style render textures, and draw one panel (the arranger) into cell 0.
- [ ] **Step 4: Run** → smoke exits 0.
- [ ] **Step 5: Commit** `git commit -m "feat(main): --ui-preview panel-mode skeleton"`

---

### Task 12: Representative panels

**Files:**
- Modify: `src/main.c` / a new `src/ui_preview.c` (preferred if it exceeds ~150 lines)
- Test: smoke with more panels

- [ ] **Step 1** Write/extend the smoke to assert the panel count string (printed once).
- [ ] **Step 2** Run → fails (only one panel).
- [ ] **Step 3** Implement panels: arranger; note-pattern; a modulation-track pattern page; one instrument panel per distinct `settings.voiceTypes` entry; route picker, delete-confirm, preset load list; routing overlay with a scripted route wired. Each renders via `DrawGUI(scene)` with the appropriate module state into its render texture.
- [ ] **Step 4** Run → smoke passes; visually inspect via a harness SHOT if useful.
- [ ] **Step 5** Commit `git commit -m "feat(ui-preview): representative screen panels"`

---

### Task 13: Hot reload of theme/layout/fonts

**Files:**
- Modify: `src/main.c` / `src/ui_preview.c`
- Test: smoke (touch layout.json mid-run is hard headless; assert the mtime-check function with a unit test)

- [ ] **Step 1** Unit test: a pure `bool fileNewerThan(const char *path, long *stamp)` helper (stat + compare, updates stamp) — true on touch, false on unchanged.
- [ ] **Step 2** Run → fails.
- [ ] **Step 3** Implement: per-frame mtime check on `layout.json`, the active theme file, and the font file; on change re-run `compileLayoutConfig` + colour-scheme reload + font reload (`InitGUI` font portion) + rebuild panel graphs.
- [ ] **Step 4** Run → unit test passes.
- [ ] **Step 5** Commit `git commit -m "feat(ui-preview): hot-reload layout/theme/fonts"`

---

## Phase E — Fonts & icons

### Task 14: Primary font swap

**Files:**
- Modify: `src/gui_core.c:205` (fallback path), `bin/cfg.json` (add font config if present)
- Test: smoke boot

- [ ] **Step 1** Boot the app under Xvfb; capture the arranger via harness SHOT as the "before".
- [ ] **Step 2** Change the fallback/default font path to `resources/fonts/04B_03__.TTF` (keep size 9). If `cfg.json` carries a `font` key, update it too.
- [ ] **Step 3** Boot + capture "after"; compare visually for overflow.
- [ ] **Step 4** Commit `git commit -m "style: swap default primary font to 04B_03"`

### Task 15: Instrument name field size

**Files:** `src/gui_instrument.c:1097-1099`
- [ ] **Step 1** Capture the instrument screen (before).
- [ ] **Step 2** Change the `DrawText` size `30` → `20` and scale `cellW` min accordingly.
- [ ] **Step 3** Capture (after); confirm 32 cells still fit.
- [ ] **Step 4** Commit `git commit -m "style: shrink instrument name field font"`

### Task 16: Vendor pixelarticons + icon font slot

**Files:**
- Create: `bin/resources/fonts/pixelart-icons-font.ttf`, `bin/resources/fonts/pixelarticons-LICENSE.txt`
- Modify: `src/gui_core.c` (load `symbolFont` from the TTF with PUA codepoints), `src/gui_style.c` (`styleFont("symbol")` unchanged), `src/meson.build`/install rules if fonts are copied
- Test: unit `iconUtf8` mapping

**Interfaces:** `const char *iconUtf8(const char *name);`

- [ ] **Step 1** Copy the TTF + license from `~/pixelarticons_pkg/pixelarticons/`.
- [ ] **Step 2** Unit test: `iconUtf8("plug")` returns non-empty UTF-8; `iconUtf8("nope")` returns "".
- [ ] **Step 3** Implement: an `IconDef { const char *name; int codepoint; }` table for the spec's icon map (codepoints from `fonts/pixelart-icons-font.css`), `LoadedFont` with those codepoints via `LoadFontEx(path, ICON_SIZE, cps, count)` into `symbolFont` (replacing the `initCustomFont(iconzfin)` call), and `iconUtf8` encoding the codepoint to a static UTF-8 buffer.
- [ ] **Step 4** Run unit test → PASS.
- [ ] **Step 5** Commit `git commit -m "feat(font): vendor pixelarticons icon font + name->glyph map"`

### Task 17: Plug glyph on route buttons + icon map rollout

**Files:** `src/gui_inst_mod.c` (route buttons), plus each feature site for the map
- [ ] **Step 1** Set the route buttons' `gn->text = iconUtf8("plug")` (Phase A text element) in all route-button sites (`appendModSourceEntry`, `appendPatternTrackRow`).
- [ ] **Step 2** Capture the routing overlay; confirm the plug renders crisply. If not, switch to a nearest-neighbour PNG atlas (flag to the user).
- [ ] **Step 3** Apply remaining map entries: attack/decay, prev/next, save/load, fm/sample/blep inst, voice, bpm, swing, pan, loop, sample start/end, playback type, voice polyphony, blep shape — replacing the relevant text/tags with `iconUtf8(...)`.
- [ ] **Step 4** Boot + capture each affected screen; verify no overlap.
- [ ] **Step 5** Commit `git commit -m "feat(icons): pixelarticons glyphs for route and UI features"`

---

## Self-Review

- **Spec coverage:** WS-A → Tasks 1-5; WS-B → Tasks 6-8; WS-C → Tasks 9-10; WS-D → Tasks 11-13; WS-E → Tasks 14-17. Theme extensions → Tasks 1-4, 6-7, 16.
- **Placeholder scan:** Tasks 5, 8, 9-17 reference specific existing code to modify rather than quoting every line; the new/leaf units (Tasks 1-3, 6-7, 16) carry complete code. Harness navigation steps are discovered by running, consistent with the repo's plan style.
- **Type consistency:** `guiNodeSetText`/`guiNodeSetSublabel`, `createTextGuiNode`, `resolveTextStyle`, `resolveCurveIconStyle`, `curveIconIndex`, `iconUtf8`, `findSourceTypeNode` are defined once and reused with the same signatures.
- **Risk:** Task 17's rollout is broad; split further if a screen changes too much. Task 7's curve-icon draw hook touches a shared dial path — verify core dials are unaffected.
