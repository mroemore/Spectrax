/*
 * styling_tutorial -- the self-contained demo behind
 * manual/tutorials/building-a-component.md.
 *
 * It builds a component that does not exist anywhere in the shipped app
 * -- a "Macro Bank" panel -- out of the JSON styling system, and then
 * proves the styling system did what the JSON asked for.
 *
 * Three rules this file follows:
 *
 *   1. It is its own build target. Nothing here is linked into
 *      `spectrax`; the demo links the style core (gui_style.c +
 *      graph_gui.c) and the raylib headers only.
 *   2. It never touches shipped behaviour. It reads its own layout.json
 *      and its own clr.json; the app's src/layout.json and bin/clr.json
 *      are not involved.
 *   3. The C code says WHAT the panel is. Every colour, size, offset
 *      and alignment comes from JSON. The only things the C code decides
 *      are the widget hierarchy, the parameter values and the draw
 *      primitives.
 *
 * Usage:
 *   styling_tutorial              draw the panel in a window (ESC quits)
 *   styling_tutorial --selftest   headless: assert the resolved styles
 *                                 and the reflowed geometry, print PASS/FAIL
 *
 * Options:
 *   --layout PATH    style config   (default: the demo's layout.json)
 *   --theme PATH     theme file     (default: the demo's clr.json)
 *   --asset-dir DIR  directory that holds resources/ (the knob asset path
 *                    in layout.json is resolved against the process CWD)
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "curve_icons.h"
#include "gui_style.h"
#include "graph_gui.h"
#include "io/config_io.h"
#include "modsystem.h"
#include "raylib.h"

/* --- the three font slots the style core expects -------------------------
 *
 * gui_style.c's finalizeStyles() takes the address of these three, so a
 * program that links the style core has to define them. The app fills
 * them in InitGUI(); the demo fills them after InitWindow(). */
Font pixelFont;
Font textFont;
Font symbolFont;

/* styleFont() lives in gui_core.c (app side). The demo ships its own
 * copy of the same three-line mapping so the demo does not have to link
 * the app's GUI layer. The semantics are identical. */
Font *styleFont(const char *name) {
	if(name && strcmp(name, "text") == 0) {
		return &textFont;
	}
	if(name && strcmp(name, "symbol") == 0) {
		return &symbolFont;
	}
	return &pixelFont;
}

/* --- demo state --------------------------------------------------------- */

static ColourScheme g_cs;      /* the demo's theme */
static Texture2D g_curveIcons; /* the curve-icon atlas (curve_icons.c) */

/* Per-node payloads. The app keeps these in typed structs that embed
 * GuiNode as their first member; the demo does the same. */
typedef struct {
	GuiNode base;
	int paletteIdx;      /* which chip.palette entry fills the chip */
	const char *typeTag; /* top-right tag, e.g. "FM" */
	const char *voiceCount; /* top-left voice count, e.g. "V:4" */
	const char *label;   /* the centred channel label */
	const char *patchName; /* bottom-left patch name */
	int activeVoices;    /* how many voice lights are lit */
} DemoChip;

typedef struct {
	GuiNode base;
	const char *note; /* pitch text, e.g. "C4" */
	bool playing;     /* draw the bgPlaying colour */
} DemoStep;

/* --- drawing helpers (the app's equivalents live in gui_core.c) --------- */

static void demoPanel(int x, int y, int w, int h, float lineW, bool selected,
                      Color border, Color borderSelected) {
	int o = (int)lineW;
	DrawRectangle(x + o, y + o, w, h, (Color){ 0, 0, 0, 140 });
	DrawRectangle(x, y, w, h, g_cs.panel);
	/* border.colorSelected only takes effect when its alpha is non-zero;
	 * otherwise the legacy highlight is kept. */
	Color outline = border;
	if(selected) {
		outline = (borderSelected.a != 0) ? borderSelected : g_cs.highlightedCell;
	}
	DrawRectangleLinesEx((Rectangle){ x, y, w, h }, lineW, outline);
}

static void demoValueBox(int x, int y, int w, int h, const char *text, Color colour, int fontSize) {
	DrawRectangle(x, y, w, h, g_cs.valueDisplayBg);
	float sz = (float)(fontSize > 0 ? fontSize : 9);
	DrawTextEx(pixelFont, text, (Vector2){ x + 4, y + 4 }, sz, 1, colour);
}

/* --- draw functions, one per style type --------------------------------- */

static void demoDrawDial(void *self) {
	GuiNode *gn = (GuiNode *)self;
	if(!gn->p) {
		return;
	}
	const DialStyle *st = resolveDialStyle(gn);
	char buf[32];
	snprintf(buf, sizeof(buf), st->value.format, gn->p->baseValue);
	float range = gn->p->maxValue - gn->p->minValue;
	float angle = 0.0f;
	if(range > 0.0f) {
		float norm = (gn->p->baseValue - gn->p->minValue) / (range / 100.0f);
		angle = norm * (st->knob.sweep / 100.0f);
	}
	DialGeometry g;
	computeDialGeometry(gn, st, &g);

	demoPanel(gn->x, gn->y, gn->w, gn->h, st->border.borderWidth, gn->selected,
	          st->border.color, st->border.colorSelected);
	if(st->knob.hasAsset && st->knob.assetTex.id != 0) {
		DrawTexturePro(st->knob.assetTex,
		               (Rectangle){ 0, 0, st->knob.assetTex.width, st->knob.assetTex.height },
		               (Rectangle){ g.knobX + st->knob.radius + st->knob.offsetX,
		                             g.knobY + st->knob.radius + st->knob.offsetY,
		                             g.knobW, g.knobH },
		               (Vector2){ g.knobW / 2.0f, g.knobH / 2.0f },
		               st->knob.startAngle + angle, WHITE);
	} else {
		/* Procedural fallback when the asset is missing. */
		DrawCircleSector((Vector2){ g.knobX + st->knob.radius, g.knobY + st->knob.radius },
		                 (float)st->knob.radius + 2, st->knob.startAngle,
		                 st->knob.startAngle + angle, 32, st->knob.color);
		DrawCircle(g.knobX + st->knob.radius, g.knobY + st->knob.radius,
		           (float)st->knob.radius - 1, g_cs.panel);
	}
	const CurveIconStyle *ci = &st->curveIcon;
	if(ci->frameCount > 0) {
		/* The curve icon replaces the numeric readout. */
		int idx = curveIconIndex(gn->p->baseValue);
		if(idx >= ci->frameCount) {
			idx = ci->frameCount - 1;
		}
		float iconX = g.knobX + (g.knobW - (float)ci->size) * 0.5f + (float)ci->offsetX;
		float iconY = g.knobY + (g.knobH - (float)ci->size) * 0.5f + (float)ci->offsetY;
		DrawTexturePro(g_curveIcons,
		               (Rectangle){ idx * (float)CURVE_ICON_SIZE, 0, (float)CURVE_ICON_SIZE, (float)CURVE_ICON_SIZE },
		               (Rectangle){ iconX, iconY, (float)ci->size, (float)ci->size },
		               (Vector2){ 0, 0 }, 0.0f, ci->color);
	} else if(g.valueW > 0 && g.valueH > 0) {
		/* value.width/height <= 0 hides the numeric entirely. */
		demoValueBox(g.valueX, g.valueY, g.valueW, g.valueH, buf, st->value.color, st->value.fontSize);
	}
	if(st->label.fontSize > 0) {
		Font *lf = styleFont(st->label.fontName);
		float lw = MeasureTextEx(*lf, gn->name, (float)st->label.fontSize, (float)st->label.spacing).x;
		DrawTextEx(*lf, gn->name, (Vector2){ g.labelX - lw * 0.5f, (float)g.labelY },
		           (float)st->label.fontSize, (float)st->label.spacing,
		           gn->selected ? st->label.colorSelected : st->label.color);
	}
}

static void demoDrawDiscreteDial(void *self) {
	GuiNode *gn = (GuiNode *)self;
	if(!gn->p) {
		return;
	}
	const DialStyle *st = resolveDiscreteDialStyle(gn);
	char buf[32];
	snprintf(buf, sizeof(buf), st->value.format, (int)gn->p->baseValue);
	DialGeometry g;
	computeDialGeometry(gn, st, &g);
	demoPanel(gn->x, gn->y, gn->w, gn->h, st->border.borderWidth, gn->selected,
	          st->border.color, st->border.colorSelected);
	DrawCircle(g.knobX + st->knob.radius, g.knobY + st->knob.radius,
	           (float)st->knob.radius, st->knob.color);
	if(g.valueW > 0 && g.valueH > 0) {
		demoValueBox(g.valueX, g.valueY, g.valueW, g.valueH, buf, st->value.color, st->value.fontSize);
	}
	if(st->label.fontSize > 0) {
		Font *lf = styleFont(st->label.fontName);
		float lw = MeasureTextEx(*lf, gn->name, (float)st->label.fontSize, (float)st->label.spacing).x;
		DrawTextEx(*lf, gn->name, (Vector2){ g.labelX - lw * 0.5f, (float)g.labelY },
		           (float)st->label.fontSize, (float)st->label.spacing,
		           gn->selected ? st->label.colorSelected : st->label.color);
	}
}

static void demoDrawBtn(void *self) {
	GuiNode *gn = (GuiNode *)self;
	const BtnStyle *st = resolveBtnStyle(gn);
	demoPanel(gn->x, gn->y, gn->w, gn->h, st->border.borderWidth, gn->selected,
	          st->border.color, st->border.colorSelected);
	Font *lf = styleFont(st->label.fontName);
	const char *label = gn->text ? gn->text : gn->name;
	DrawTextEx(*lf, label,
	           (Vector2){ (float)(gn->x + gn->padding + st->label.offsetX),
	                       (float)(gn->y + gn->padding + st->label.offsetY) },
	           (float)st->label.fontSize, (float)st->label.spacing,
	           gn->selected ? st->label.colorSelected : st->label.color);
	if(st->sublabel.fontSize > 0 && gn->sublabel) {
		/* The sublabel is centred on the node and anchored to the node's
		 * top edge -- the layout padding is NOT added. */
		Font *sf = styleFont(st->sublabel.fontName);
		Vector2 m = MeasureTextEx(*sf, gn->sublabel, (float)st->sublabel.fontSize, (float)st->sublabel.spacing);
		DrawTextEx(*sf, gn->sublabel,
		           (Vector2){ (float)gn->x + ((float)gn->w - m.x) * 0.5f,
		                       (float)gn->y + (float)st->sublabel.offsetY },
		           (float)st->sublabel.fontSize, (float)st->sublabel.spacing,
		           gn->selected ? st->sublabel.colorSelected : st->sublabel.color);
	}
}

static void demoDrawTypeLabel(void *self) {
	GuiNode *gn = (GuiNode *)self;
	const TypeLabelStyle *st = resolveTypeLabelStyle(gn);
	const char *txt = gn->text ? gn->text : gn->name;
	Font *lf = styleFont(st->label.fontName);
	Vector2 m = MeasureTextEx(*lf, txt, (float)st->label.fontSize, (float)st->label.spacing);
	DrawTextEx(*lf, txt,
	           (Vector2){ (float)gn->x + ((float)gn->w - m.x) * 0.5f + (float)st->label.offsetX,
	                       (float)gn->y + ((float)gn->h - m.y) * 0.5f + (float)st->label.offsetY },
	           (float)st->label.fontSize, (float)st->label.spacing,
	           gn->selected ? st->label.colorSelected : st->label.color);
	/* A type-label draws its border only when it holds the cursor. */
	if(gn->selected) {
		Color outline = (st->border.colorSelected.a != 0) ? st->border.colorSelected : st->border.color;
		DrawRectangleLinesEx((Rectangle){ gn->x, gn->y, gn->w, gn->h }, st->border.borderWidth, outline);
	}
}

static void demoDrawRouteDest(void *self) {
	GuiNode *gn = (GuiNode *)self;
	const DestStyle *st = resolveDestStyle(gn);
	Color outline = gn->selected ? st->border.colorSelected : st->border.color;
	DrawRectangleLinesEx((Rectangle){ gn->x, gn->y, gn->w, gn->h }, st->border.borderWidth, outline);
}

static void demoDrawText(void *self) {
	GuiNode *gn = (GuiNode *)self;
	const TextStyle *st = resolveTextStyle(gn);
	Font *lf = styleFont(st->fontName);
	const char *txt = gn->text ? gn->text : "";
	Vector2 m = MeasureTextEx(*lf, txt, (float)st->fontSize, (float)st->spacing);
	float tx = (float)gn->x + (float)st->offsetX;
	float ty = (float)gn->y + (float)st->offsetY;
	if(st->hAlign == TXT_ALIGN_CENTER) {
		tx += ((float)gn->w - m.x) * 0.5f;
	} else if(st->hAlign == TXT_ALIGN_RIGHT) {
		tx += (float)gn->w - m.x;
	}
	if(st->vAlign == TXT_ALIGN_MIDDLE) {
		ty += ((float)gn->h - m.y) * 0.5f;
	} else if(st->vAlign == TXT_ALIGN_BOTTOM) {
		ty += (float)gn->h - m.y;
	}
	DrawTextEx(*lf, txt, (Vector2){ tx, ty }, (float)st->fontSize, (float)st->spacing,
	           gn->selected ? st->colorSelected : st->color);
}

static void demoDrawChip(void *self) {
	DemoChip *chip = (DemoChip *)self;
	GuiNode *gn = &chip->base;
	const ChipStyle *st = resolveChipStyle(gn);
	int idx = chip->paletteIdx;
	if(idx < 0) {
		idx = 0;
	}
	if(idx > 7) {
		idx = idx % 8;
	}
	DrawRectangle(gn->x, gn->y, gn->w, gn->h, st->palette[idx]);
	DrawRectangleLinesEx((Rectangle){ gn->x, gn->y, gn->w, gn->h }, st->border.borderWidth,
	                     gn->selected ? st->border.colorSelected : st->border.color);

	ChipGeometry g;
	computeChipGeometry(gn, st, chip->typeTag, chip->label, &g);

	Font *fvc = styleFont(st->voiceCount.fontName);
	Font *ftt = styleFont(st->typeTag.fontName);
	Font *flb = styleFont(st->label.fontName);
	Font *fpn = styleFont(st->patchName.fontName);

	DrawTextEx(*fvc, chip->voiceCount, (Vector2){ (float)g.voiceCountX, (float)g.voiceCountY },
	           (float)st->voiceCount.fontSize, (float)st->voiceCount.spacing, st->voiceCount.color);
	DrawTextEx(*ftt, chip->typeTag, (Vector2){ (float)g.typeTagX, (float)g.typeTagY },
	           (float)st->typeTag.fontSize, (float)st->typeTag.spacing, st->typeTag.color);
	DrawTextEx(*flb, chip->label, (Vector2){ (float)g.labelX, (float)g.labelY },
	           (float)st->label.fontSize, (float)st->label.spacing, st->label.color);
	DrawTextEx(*fpn, chip->patchName, (Vector2){ (float)g.patchNameX, (float)g.patchNameY },
	           (float)st->patchName.fontSize, (float)st->patchName.spacing, st->patchName.color);

	/* Voice lights: one square per voice, bottom-right. */
	int dotSize = st->dots.size;
	int dotGap = st->dots.gap;
	int rowW = chip->activeVoices * dotSize + (chip->activeVoices > 0 ? (chip->activeVoices - 1) * dotGap : 0);
	int startX = gn->x + gn->w - rowW - 3;
	for(int i = 0; i < chip->activeVoices; i++) {
		Color c = (i < 2) ? st->dots.colourActive : st->dots.colour;
		DrawRectangle(startX + i * (dotSize + dotGap), g.dotsY, dotSize, dotSize, c);
	}
}

static void demoDrawStep(void *self) {
	DemoStep *step = (DemoStep *)self;
	GuiNode *gn = &step->base;
	const StepCellStyle *st = resolveStepCellStyle(gn);
	if(gn->selected) {
		/* The surround is drawn 3 px outside the cell. */
		DrawRectangle(gn->x - 3, gn->y - 3, gn->w + 6, gn->h + 6, st->borderSelected);
	}
	DrawRectangleRec((Rectangle){ gn->x, gn->y, gn->w, gn->h },
	                 step->playing ? st->bgPlaying : st->bgEmpty);
	Font *font = styleFont(st->note.fontName);
	float fontSize = st->note.fontSize ? (float)st->note.fontSize : (float)textFont.baseSize;
	DrawTextEx(*font, step->note ? step->note : "",
	           (Vector2){ (float)(gn->x + st->note.offsetX), (float)(gn->y + st->note.offsetY) },
	           fontSize, (float)st->note.spacing, st->note.color);
}

/* --- node factories (the app's equivalents live in gui_core.c) ---------- */

static GuiNode *demoDialNode(const char *name, Parameter *p) {
	GuiNode *gn = createGuiNode(0, 0, 40, 40, 0, na_horizontal, name, true, false);
	gn->p = p;
	gn->drawable = true;
	gn->draw = demoDrawDial;
	return gn;
}

static GuiNode *demoDiscreteDialNode(const char *name, Parameter *p) {
	GuiNode *gn = createGuiNode(0, 0, 40, 40, 0, na_horizontal, name, true, false);
	gn->p = p;
	gn->drawable = true;
	gn->draw = demoDrawDiscreteDial;
	return gn;
}

static GuiNode *demoBtnNode(const char *text, const char *sublabel) {
	GuiNode *gn = createGuiNode(0, 0, 60, 30, 0, na_horizontal, text, true, false);
	guiNodeSetText(gn, text);
	if(sublabel) {
		guiNodeSetSublabel(gn, sublabel);
	}
	gn->drawable = true;
	gn->draw = demoDrawBtn;
	return gn;
}

static GuiNode *demoTypeLabelNode(const char *text) {
	GuiNode *gn = createGuiNode(0, 0, 40, 20, 0, na_horizontal, text, true, false);
	guiNodeSetText(gn, text);
	gn->drawable = true;
	gn->draw = demoDrawTypeLabel;
	return gn;
}

static GuiNode *demoRouteDestNode(const char *name) {
	GuiNode *gn = createGuiNode(0, 0, 40, 30, 0, na_horizontal, name, true, false);
	gn->drawable = true;
	gn->draw = demoDrawRouteDest;
	return gn;
}

static GuiNode *demoTextNode(const char *text, const char *className) {
	GuiNode *gn = createGuiNode(0, 0, 100, 12, 0, na_horizontal, text, false, false);
	guiNodeSetText(gn, text);
	if(className) {
		guiNodeSetClass(gn, className);
	}
	gn->drawable = true;
	gn->draw = demoDrawText;
	return gn;
}

static GuiNode *demoChipNode(int paletteIdx, const char *typeTag, const char *voiceCount,
                             const char *label, const char *patchName, int activeVoices) {
	DemoChip *chip = (DemoChip *)malloc(sizeof(DemoChip));
	if(!chip) {
		return NULL;
	}
	memset(chip, 0, sizeof(*chip));
	chip->base = *createGuiNode(0, 0, 120, 30, 0, na_horizontal, typeTag, true, false);
	chip->paletteIdx = paletteIdx;
	chip->typeTag = typeTag;
	chip->voiceCount = voiceCount;
	chip->label = label;
	chip->patchName = patchName;
	chip->activeVoices = activeVoices;
	chip->base.drawable = true;
	chip->base.draw = demoDrawChip;
	return &chip->base;
}

static GuiNode *demoStepNode(const char *note, bool playing) {
	DemoStep *step = (DemoStep *)malloc(sizeof(DemoStep));
	if(!step) {
		return NULL;
	}
	memset(step, 0, sizeof(*step));
	step->base = *createGuiNode(0, 0, 24, 16, 0, na_horizontal, note, true, false);
	step->note = note;
	step->playing = playing;
	step->base.drawable = true;
	step->base.draw = demoDrawStep;
	return &step->base;
}

/* --- the component ------------------------------------------------------ */

/* Parameters the dials drive. A real component would own these; the
 * demo only needs something with a baseValue, a min and a max. */
static Parameter g_params[5];

static void initParams(void) {
	memset(g_params, 0, sizeof(g_params));
	g_params[0].baseValue = 0.75f;  g_params[0].minValue = 0.0f; g_params[0].maxValue = 1.0f;
	g_params[1].baseValue = 2.50f;  g_params[1].minValue = 0.5f; g_params[1].maxValue = 8.0f;
	g_params[2].baseValue = 0.35f;  g_params[2].minValue = 0.0f; g_params[2].maxValue = 1.0f;
	g_params[3].baseValue = 3.0f;   g_params[3].minValue = 0.0f; g_params[3].maxValue = 7.0f;
	g_params[4].baseValue = 0.60f;  g_params[4].minValue = 0.0f; g_params[4].maxValue = 1.0f;
}

#define MACRO_STEPS 8
#define MACRO_ROW_H 16

typedef struct {
	GuiNode *bank;
	GuiNode *dialRow;
	GuiNode *actionRow;
	ScrollContainer *steps;
	GuiNode *dials[5];
	GuiNode *typeTag;
	GuiNode *bypass;
	GuiNode *chip;
	GuiNode *slot;
	GuiNode *stepCells[MACRO_STEPS];
} MacroBank;

/* Build the Macro Bank: a panel that exists only in this demo.
 *
 * Note the order: every child is appended BEFORE applyLayout() runs.
 * applyLayout() only touches children that already exist. */
static MacroBank buildMacroBank(int x, int y, int w, int h) {
	MacroBank mb;
	memset(&mb, 0, sizeof(mb));

	mb.bank = createGuiNode(x, y, w, h, 2, na_vertical, "macro-bank", false, false);

	/* 1. The title. */
	GuiNode *title = demoTextNode("MACRO BANK", NULL);
	appendItem(mb.bank, title, 1);

	/* 2. The dial row -- a container that is itself a child of the bank
	 *    and carries its own layout (a nested layout). */
	mb.dialRow = createGuiNode(x, y, w, h, 1, na_horizontal, "macro-dials", false, false);

	mb.dials[0] = demoDialNode("AMT", &g_params[0]);
	/* Binding #1: the class is set in C, by name. */
	guiNodeSetClass(mb.dials[0], "macro-amount");
	appendItem(mb.dialRow, mb.dials[0], 1);

	mb.dials[1] = demoDialNode("RAT", &g_params[1]); /* class: macro-ratio  (childClasses) */
	appendItem(mb.dialRow, mb.dials[1], 1);

	mb.dials[2] = demoDialNode("SPR", &g_params[2]); /* class: macro-plain  (hollow class) */
	appendItem(mb.dialRow, mb.dials[2], 1);

	mb.dials[3] = demoDiscreteDialNode("MODE", &g_params[3]); /* class: macro-mode */
	appendItem(mb.dialRow, mb.dials[3], 1);

	mb.dials[4] = demoDialNode("CRV", &g_params[4]); /* class: macro-curve (curve icon) */
	appendItem(mb.dialRow, mb.dials[4], 1);

	appendItem(mb.bank, mb.dialRow, 1);

	/* 3. The action row -- the other nested layout. */
	mb.actionRow = createGuiNode(x, y, w, h, 1, na_horizontal, "macro-actions", false, false);
	mb.typeTag = demoTypeLabelNode("MACRO");
	appendItem(mb.actionRow, mb.typeTag, 1);
	mb.bypass = demoBtnNode("BYPASS", "ON");
	appendItem(mb.actionRow, mb.bypass, 1);
	mb.chip = demoChipNode(3, "FM", "V:4", "MACRO", "INIT PATCH", 3);
	appendItem(mb.actionRow, mb.chip, 1);
	mb.slot = demoRouteDestNode("MACRO 4");
	appendItem(mb.actionRow, mb.slot, 1);
	appendItem(mb.bank, mb.actionRow, 1);

	/* 4. The step strip -- a scroll container. Its children are laid out
	 *    in fixed-height rows, not by weight. */
	mb.steps = (ScrollContainer *)createScrollContainer(x, y, w, h, MACRO_ROW_H, "macro-steps");
	static const char *const notes[MACRO_STEPS] = { "C4", "D4", "E4", "G4", "A4", "C5", "D5", "E5" };
	for(int i = 0; i < MACRO_STEPS; i++) {
		GuiNode *cell = demoStepNode(notes[i], i == 3);
		if(i == 0) {
			guiNodeSetClass(cell, "macro-step"); /* a variant */
		} else if(i == 1) {
			guiNodeSetClass(cell, "step-cell"); /* the per-type default class, named */
		}
		/* cells 2..7 carry no class at all -> the baked default */
		mb.stepCells[i] = cell;
		appendItem(&mb.steps->base, cell, 1);
	}
	appendItem(mb.bank, &mb.steps->base, 1);

	/* 5. The hint line. */
	GuiNode *hint = demoTextNode("SPREAD IS FM-ONLY", NULL);
	appendItem(mb.bank, hint, 1);

	/* Layouts last: inner ones first, then the outer one. */
	applyLayout(mb.dialRow, "macro-dial-row");
	applyLayout(mb.actionRow, "macro-action-row");
	applyLayout(mb.bank, "macro-bank");
	return mb;
}

/* --- config validation (the demo's own copy of the shipped rules) ------- */

/* Mirrors tests/dsp/test_layout.c's shipped-config guard: every colour
 * value must name a theme token, every childClasses entry must name a
 * class that exists, and the weights/childClasses lengths must match.
 * The shipped validator runs the same rules over src/layout.json. */
static int validateLayoutFile(const char *path) {
	FILE *f = fopen(path, "rb");
	if(!f) {
		printf("FAIL: cannot open %s\n", path);
		return 0;
	}
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = (char *)malloc((size_t)sz + 1);
	if(!buf) {
		fclose(f);
		return 0;
	}
	size_t rd = fread(buf, 1, (size_t)sz, f);
	buf[rd] = '\0';
	fclose(f);
	cJSON *doc = cJSON_Parse(buf);
	free(buf);
	if(!doc) {
		printf("FAIL: %s is not valid JSON\n", path);
		return 0;
	}
	int ok = 1;
	cJSON *styles = cJSON_GetObjectItemCaseSensitive(doc, "styles");
	if(cJSON_IsObject(styles)) {
		cJSON *it = NULL;
		cJSON_ArrayForEach(it, styles) {
			if(styleClassType(it->string) < 0) {
				printf("FAIL: style class '%s' did not compile\n", it->string);
				ok = 0;
			}
			cJSON *parent = cJSON_GetObjectItemCaseSensitive(it, "extends");
			if(cJSON_IsString(parent) &&
			   cJSON_GetObjectItemCaseSensitive(styles, parent->valuestring) == NULL) {
				printf("FAIL: style '%s' extends missing class '%s'\n", it->string,
				       parent->valuestring);
				ok = 0;
			}
		}
	}
	cJSON *layouts = cJSON_GetObjectItemCaseSensitive(doc, "layouts");
	if(cJSON_IsObject(layouts)) {
		cJSON *l = NULL;
		cJSON_ArrayForEach(l, layouts) {
			if(layoutByName(l->string) == NULL) {
				printf("FAIL: layout '%s' did not register\n", l->string);
				ok = 0;
			}
			cJSON *weights = cJSON_GetObjectItemCaseSensitive(l, "weights");
			cJSON *cc = cJSON_GetObjectItemCaseSensitive(l, "childClasses");
			if(cJSON_IsArray(weights) && cJSON_IsArray(cc) &&
			   cJSON_GetArraySize(weights) != cJSON_GetArraySize(cc)) {
				printf("FAIL: layout '%s' has %d weights but %d childClasses\n",
				       l->string, cJSON_GetArraySize(weights), cJSON_GetArraySize(cc));
				ok = 0;
			}
			if(cJSON_IsArray(cc)) {
				int n = cJSON_GetArraySize(cc);
				for(int i = 0; i < n; i++) {
					cJSON *e = cJSON_GetArrayItem(cc, i);
					if(cJSON_IsString(e) && e->valuestring[0] &&
					   styleClassType(e->valuestring) < 0) {
						printf("FAIL: layout '%s' childClasses[%d] names unknown class '%s'\n",
						       l->string, i, e->valuestring);
						ok = 0;
					}
				}
			}
		}
	}
	cJSON_Delete(doc);
	return ok;
}

/* Every colour-looking key in the config must name a theme token. */
static int validateColourTokens(const char *path) {
	static const char *const colourKeys[] = {
		"color", "colorSelected", "bgEmpty", "bgPlaying", "borderSelected",
		"colour", "colourActive", NULL
	};
	FILE *f = fopen(path, "rb");
	if(!f) {
		return 0;
	}
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = (char *)malloc((size_t)sz + 1);
	if(!buf) {
		fclose(f);
		return 0;
	}
	size_t rd = fread(buf, 1, (size_t)sz, f);
	buf[rd] = '\0';
	fclose(f);
	cJSON *doc = cJSON_Parse(buf);
	free(buf);
	if(!doc) {
		return 0;
	}
	int ok = 1;
	cJSON *styles = cJSON_GetObjectItemCaseSensitive(doc, "styles");
	cJSON *it = NULL;
	cJSON_ArrayForEach(it, styles) {
		cJSON *sub = NULL;
		cJSON_ArrayForEach(sub, it) {
			bool isColour = false;
			for(int i = 0; colourKeys[i]; i++) {
				if(sub->string && strcmp(colourKeys[i], sub->string) == 0) {
					isColour = true;
				}
			}
			if(isColour) {
				if(!cJSON_IsString(sub) || !themeFieldByName(&g_cs, sub->valuestring)) {
					printf("FAIL: %s.%s is not a theme colour token\n", it->string,
					       sub->string ? sub->string : "?");
					ok = 0;
				}
			}
			if(sub->string && strcmp(sub->string, "palette") == 0 && cJSON_IsArray(sub)) {
				int n = cJSON_GetArraySize(sub);
				for(int i = 0; i < n; i++) {
					cJSON *e = cJSON_GetArrayItem(sub, i);
					if(!cJSON_IsString(e) || !themeFieldByName(&g_cs, e->valuestring)) {
						printf("FAIL: %s.palette[%d] is not a theme colour token\n", it->string, i);
						ok = 0;
					}
				}
			}
		}
	}
	cJSON_Delete(doc);
	return ok;
}

/* --- self test ---------------------------------------------------------- */

static int g_failures = 0;

#define CHECK(cond, msg) do { \
	if(!(cond)) { \
		printf("FAIL: %s\n", msg); \
		g_failures++; \
	} \
} while(0)

static void checkStyles(void) {
	/* Every class resolved to the type it should have. */
	CHECK(styleClassType("dial") == STYLE_DIAL, "dial is a dial class");
	CHECK(styleClassType("dial-discrete") == STYLE_DIAL_DISCRETE, "dial-discrete is discrete");
	CHECK(styleClassType("macro-amount") == STYLE_DIAL, "macro-amount is a dial class");
	CHECK(styleClassType("macro-mode") == STYLE_DIAL_DISCRETE, "macro-mode is discrete");
	CHECK(styleClassType("macro-bypass") == STYLE_BTN, "macro-bypass is a btn class");
	CHECK(styleClassType("macro-type") == STYLE_TYPE_LABEL, "macro-type is a type-label");
	CHECK(styleClassType("macro-slot") == STYLE_DEST, "macro-slot is a route-dest");
	CHECK(styleClassType("macro-step") == STYLE_STEP_CELL, "macro-step is a step-cell");
	CHECK(styleClassType("macro-title") == STYLE_TEXT, "macro-title is a text class");
	CHECK(styleClassType("chip") == STYLE_CHIP, "chip is a chip class");

	GuiNode *n = createBlankGuiNode();

	/* Multi-level chain: macro-amount -> macro-dial -> dial.
	 * macro-dial overrides the knob (16) and the value format (%03.1f);
	 * macro-amount overrides the knob again (22). The dial class
	 * contributes the sweep and the asset. */
	guiNodeSetClass(n, "macro-amount");
	const DialStyle *amt = resolveDialStyle(n);
	CHECK(amt->knob.size == 22, "macro-amount knob.size 22 (its own)");
	CHECK(amt->knob.radius == 11, "macro-amount knob.radius 11 (its own)");
	CHECK(strcmp(amt->value.format, "%03.1f") == 0, "macro-amount inherits macro-dial's format");
	CHECK(amt->label.fontSize == 9, "macro-amount label.fontSize 9 (its own)");
	CHECK(amt->knob.sweep == 270, "macro-amount inherits dial's sweep");
	CHECK(amt->knob.hasAsset, "macro-amount inherits dial's knob asset");
	CHECK(strcmp(amt->knob.assetPath, "resources/images/dial2.png") == 0, "asset path");

	/* The hollow class resolves to exactly its parent. */
	guiNodeSetClass(n, "macro-plain");
	const DialStyle *plain = resolveDialStyle(n);
	guiNodeSetClass(n, "dial");
	const DialStyle *base = resolveDialStyle(n);
	CHECK(plain->knob.size == base->knob.size, "hollow class: same knob size as its parent");
	CHECK(plain->knob.radius == base->knob.radius, "hollow class: same radius");
	CHECK(strcmp(plain->value.format, base->value.format) == 0, "hollow class: same value format");
	CHECK(plain->label.fontSize == base->label.fontSize, "hollow class: same label size");
	CHECK(plain->knob.size == 18, "hollow class: the JSON dial's knob, not the baked default (20)");

	/* The numeric readout is hidden and the curve icon takes its place. */
	guiNodeSetClass(n, "macro-curve");
	const DialStyle *curve = resolveDialStyle(n);
	CHECK(curve->value.width == 0 && curve->value.height == 0, "macro-curve hides the numeric");
	CHECK(curve->curveIcon.frameCount == 32, "macro-curve curveIcon.frameCount 32");
	CHECK(curve->curveIcon.size == 12, "macro-curve curveIcon.size 12");
	CHECK(curve->curveIcon.color.r == g_cs.valueText.r &&
	      curve->curveIcon.color.g == g_cs.valueText.g &&
	      curve->curveIcon.color.b == g_cs.valueText.b,
	      "macro-curve curveIcon.color is text.value");

	/* The discrete dial: its own type, and a top-aligned knob so the
	 * readout can sit underneath it. A discrete class resolves through
	 * resolveDiscreteDialStyle(), not resolveDialStyle(). */
	guiNodeSetClass(n, "macro-mode");
	const DialStyle *mode = resolveDiscreteDialStyle(n);
	CHECK(mode->vAlign == DIAL_VALIGN_TOP, "macro-mode vAlign is top");
	CHECK(mode->knob.size == 14, "macro-mode knob.size 14");
	CHECK(strcmp(mode->value.format, "%i") == 0, "macro-mode inherits the discrete format");
	CHECK(mode->value.offsetY == 16, "macro-mode value below the knob");

	/* Colours are tokens, resolved against the theme. */
	guiNodeSetClass(n, "macro-hint");
	const TextStyle *hint = resolveTextStyle(n);
	CHECK(hint->color.r == g_cs.disabledText.r && hint->color.g == g_cs.disabledText.g &&
	      hint->color.b == g_cs.disabledText.b && hint->color.a == 255,
	      "macro-hint uses the text.disabled token");
	CHECK(hint->hAlign == TXT_ALIGN_CENTER, "macro-hint hAlign center");
	CHECK(hint->vAlign == TXT_ALIGN_MIDDLE, "macro-hint vAlign middle");

	guiNodeSetClass(n, "macro-title");
	const TextStyle *title = resolveTextStyle(n);
	CHECK(title->fontSize == 12, "macro-title fontSize 12");
	CHECK(title->spacing == 2, "macro-title spacing 2");

	/* The chip's palette and dots come from the JSON. */
	guiNodeSetClass(n, "chip");
	const ChipStyle *chip = resolveChipStyle(n);
	CHECK(chip->palette[0].r == g_cs.chipPalette0.r, "chip palette[0] is chip.palette0");
	CHECK(chip->palette[3].r == g_cs.chipPalette3.r, "chip palette[3] is chip.palette3");
	CHECK(chip->dots.size == 5 && chip->dots.gap == 3, "chip dots 5/3");
	CHECK(chipComponentHeight(chip) == 12 + 8 + 5, "chip component height");

	/* The step-cell's note style. */
	guiNodeSetClass(n, "macro-step");
	const StepCellStyle *step = resolveStepCellStyle(n);
	CHECK(step->bgEmpty.r == g_cs.blankCell.r, "macro-step bgEmpty is cell.blank");
	CHECK(step->bgPlaying.r == g_cs.selectedCell.r, "macro-step bgPlaying is cell.selected");
	CHECK(step->borderSelected.r == g_cs.labelSelected.r, "macro-step borderSelected");
	CHECK(step->note.fontSize == 10, "macro-step note.fontSize 10");
	CHECK(step->note.offsetX == 3 && step->note.offsetY == 2, "macro-step note offsets");

	/* The btn's sublabel. */
	guiNodeSetClass(n, "macro-bypass");
	const BtnStyle *btn = resolveBtnStyle(n);
	CHECK(btn->label.fontSize == 11, "macro-bypass label.fontSize 11");
	CHECK(btn->sublabel.fontSize == 8, "macro-bypass sublabel.fontSize 8");
	CHECK(btn->sublabel.offsetY == 18, "macro-bypass sublabel.offsetY 18");
	CHECK(btn->border.color.r == g_cs.reddish.r, "macro-bypass border is control.accent");

	/* The type-label. */
	guiNodeSetClass(n, "macro-type");
	const TypeLabelStyle *tl = resolveTypeLabelStyle(n);
	CHECK(tl->label.fontSize == 11, "macro-type label.fontSize 11");

	/* The route-dest slot. */
	guiNodeSetClass(n, "macro-slot");
	const DestStyle *dest = resolveDestStyle(n);
	CHECK(dest->border.borderWidth == 1.0f, "macro-slot borderWidth 1.0");
	CHECK(dest->border.color.g == g_cs.vline.g, "macro-slot border is line.grid");

	/* An unknown class falls through to the type-named class, silently. */
	guiNodeSetClass(n, "no-such-class");
	const DialStyle *fb = resolveDialStyle(n);
	CHECK(fb->knob.size == 18, "unknown class falls through to the type-named entry");

	freeGuiNode(n);
}

static void checkRawHexIsNotAToken(const char *layoutPath) {
	/* A colour field only accepts a theme token. A raw hex string is
	 * not a token, so the field keeps the inherited value. */
	mkdir(".tmp_files", 0755);
	const char *path = ".tmp_files/styling_tutorial_hex.json";
	FILE *f = fopen(path, "w");
	if(!f) {
		printf("FAIL: cannot write %s\n", path);
		g_failures++;
		return;
	}
	fputs("{\"styles\":{\"dial\":{\"label\":{\"color\":\"text.primary\"}},"
	      "\"hex-dial\":{\"extends\":\"dial\",\"label\":{\"color\":\"#FF00FF\"}}}}", f);
	fclose(f);
	ColourScheme cs;
	memset(&cs, 0, sizeof(cs));
	cs.fontColour = (Color){ 11, 22, 33, 255 };
	cs.label = (Color){ 44, 55, 66, 255 };
	cs.labelSelected = cs.label;
	cs.dial = cs.label;
	cs.panelBorder = cs.label;
	cs.valueText = cs.label;
	CHECK(compileLayoutConfig(path, &cs), "compile the hex fixture");

	GuiNode *n = createBlankGuiNode();
	guiNodeSetClass(n, "hex-dial");
	const DialStyle *d = resolveDialStyle(n);
	CHECK(d->label.color.r == 11 && d->label.color.g == 22 && d->label.color.b == 33,
	      "a raw hex colour is ignored: the inherited token colour survives");
	CHECK(!(d->label.color.r == 255 && d->label.color.g == 0 && d->label.color.b == 255),
	      "the raw hex value is NOT applied");
	freeGuiNode(n);

	/* A second compileLayoutConfig REPLACES the registry: the demo's
	 * classes and layouts are gone until the demo config is compiled
	 * again. Nothing is appended to the old config. */
	CHECK(styleClassType("macro-amount") < 0, "the second compile wiped the class registry");
	CHECK(layoutByName("macro-bank") == NULL, "the second compile wiped the layout table");
	CHECK(compileLayoutConfig(layoutPath, &g_cs), "re-compile the demo config");
	CHECK(styleClassType("macro-amount") == STYLE_DIAL, "the demo's classes are back");
	CHECK(layoutByName("macro-bank") != NULL, "the demo's layouts are back");

	remove(path);
}

static void checkGeometry(MacroBank *mb) {
	GuiNode *bank = mb->bank;
	CHECK(bank->nodeAlignment == na_vertical, "the bank is a vertical column");
	CHECK(bank->padding == 2, "the bank takes the layout's padding");
	CHECK(bank->gap == 2, "the bank takes the layout's gap");

	/* The title and the hint are bound positionally by childClasses. */
	GuiNode *title = *(GuiNode **)bank->items->head->data;
	CHECK(title->className && strcmp(title->className, "macro-title") == 0,
	      "childClasses[0] bound the title");
	GuiNode *hint = *(GuiNode **)bank->items->tail->data;
	CHECK(hint->className && strcmp(hint->className, "macro-hint") == 0,
	      "childClasses[4] bound the hint");

	/* The dial row: weights, gap, and the exact fill. */
	GuiNode *row = mb->dialRow;
	CHECK(row->nodeAlignment == na_horizontal, "the dial row is horizontal");
	CHECK(row->gap == 2, "the dial row takes the layout's gap");
	int contentW = (int)row->w - 2 * (int)row->padding;
	int gaps = (int)row->gap * 4; /* gap * (children - 1) */
	int sum = 0;
	for(int i = 0; i < 5; i++) {
		sum += mb->dials[i]->w;
	}
	CHECK(sum == contentW - gaps, "the dial row fills its content width, minus the gaps");
	CHECK(mb->dials[1]->x == mb->dials[0]->x + mb->dials[0]->w + 2, "the gap sits between children");
	CHECK(mb->dials[0]->w > mb->dials[3]->w, "the 3-weight dials are wider than the 2-weight ones");
	CHECK(mb->dials[0]->w > 100, "the dial cell is wide enough for knob + value");

	/* Three ways to bind a class. */
	CHECK(mb->dials[0]->className && strcmp(mb->dials[0]->className, "macro-amount") == 0,
	      "the C-set class survived: childClasses[0] was empty");
	CHECK(mb->dials[1]->className && strcmp(mb->dials[1]->className, "macro-ratio") == 0,
	      "childClasses[1] bound macro-ratio");
	CHECK(mb->dials[2]->className && strcmp(mb->dials[2]->className, "macro-plain") == 0,
	      "childClasses[2] bound the hollow class");
	CHECK(mb->dials[3]->className && strcmp(mb->dials[3]->className, "macro-mode") == 0,
	      "childClasses[3] bound macro-mode");
	CHECK(mb->dials[4]->className && strcmp(mb->dials[4]->className, "macro-curve") == 0,
	      "childClasses[4] bound macro-curve");
	CHECK(mb->chip->className && strcmp(mb->chip->className, "chip") == 0,
	      "the chip is bound to the per-type default class");
	CHECK(mb->stepCells[1]->className && strcmp(mb->stepCells[1]->className, "step-cell") == 0,
	      "the second step cell names the per-type default class");
	CHECK(mb->stepCells[2]->className == NULL, "the third step cell has no class at all");

	/* A classless node resolves through the type-named class, so it lands
	 * on the JSON "chip" entry (dots 5/3) like an explicitly-named one.
	 * The baked default (4/2) is only reachable when the config declares
	 * no class of that name — which this demo's config does not, so it
	 * cannot be observed as a distinct struct here. The guide's §5.6 has
	 * the no-config case. */
	const ChipStyle *named = resolveChipStyle(mb->chip);
	CHECK(named->dots.size == 5, "the named chip class is the compiled one");
	GuiNode *plain = createBlankGuiNode();
	const ChipStyle *unclassed = resolveChipStyle(plain);
	CHECK(unclassed->dots.size == 5, "a classless node resolves to the type-named class");
	CHECK(unclassed == named, "classless and explicitly-named hit the same entry");
	freeGuiNode(plain);

	/* The scroll container. */
	/* vAlign: a top-aligned dial pins its knob to the cell's padded
	 * top, which is what leaves room for the readout underneath; a
	 * centred one floats in the middle of the cell. */
	{
		const DialStyle *mode = resolveDiscreteDialStyle(mb->dials[3]);
		DialGeometry mg;
		computeDialGeometry(mb->dials[3], mode, &mg);
		CHECK(mg.knobY == mb->dials[3]->y + mb->dials[3]->padding,
		      "the top-aligned knob sits at the cell's padded top");
		CHECK(mg.valueY == mg.knobY + 16, "the readout sits under the knob");

		const DialStyle *amt = resolveDialStyle(mb->dials[0]);
		DialGeometry ag;
		computeDialGeometry(mb->dials[0], amt, &ag);
		CHECK(ag.knobY > mb->dials[0]->y, "the centred knob floats in the cell");
	}

	ScrollContainer *sc = mb->steps;
	CHECK(sc->contentH == MACRO_STEPS * MACRO_ROW_H, "scroll content height = rows * rowH");
	int y0 = mb->stepCells[0]->y;
	int y1 = mb->stepCells[1]->y;
	CHECK(y1 - y0 == MACRO_ROW_H, "the scroll rows are rowH apart");
	/* Changing scrollOffset only moves the children after a reflow. */
	sc->scrollOffset = 32;
	reflowCoordinates(&sc->base);
	CHECK(mb->stepCells[0]->y == y0 - 32, "the reflow moved the rows by scrollOffset");
	CHECK(mb->stepCells[1]->y == y1 - 32, "every row moved by scrollOffset");
	sc->scrollOffset = 0;
	reflowCoordinates(&sc->base);
	CHECK(mb->stepCells[0]->y == y0, "the rows came back");
}

static void checkWeightsAreRatios(const char *layoutPath) {
	/* [3,3,3,2,2] and [30,30,30,20,20] describe the same row: weights are
	 * ratios, not pixels. The demo config is copied with the dial row's
	 * weights scaled by ten. */
	mkdir(".tmp_files", 0755);
	const char *path = ".tmp_files/styling_tutorial_weights.json";
	FILE *src = fopen(layoutPath, "rb");
	if(!src) {
		printf("FAIL: cannot open %s\n", layoutPath);
		g_failures++;
		return;
	}
	fseek(src, 0, SEEK_END);
	long sz = ftell(src);
	fseek(src, 0, SEEK_SET);
	char *buf = (char *)malloc((size_t)sz + 1);
	if(!buf) {
		fclose(src);
		printf("FAIL: out of memory\n");
		g_failures++;
		return;
	}
	size_t rd = fread(buf, 1, (size_t)sz, src);
	buf[rd] = '\0';
	fclose(src);
	cJSON *doc = cJSON_Parse(buf);
	free(buf);
	if(!doc) {
		printf("FAIL: %s does not parse\n", layoutPath);
		g_failures++;
		return;
	}
	cJSON *layouts = cJSON_GetObjectItemCaseSensitive(doc, "layouts");
	cJSON *row = cJSON_GetObjectItemCaseSensitive(layouts, "macro-dial-row");
	cJSON *weights = cJSON_GetObjectItemCaseSensitive(row, "weights");
	int n = cJSON_GetArraySize(weights);
	for(int i = 0; i < n; i++) {
		cJSON *e = cJSON_GetArrayItem(weights, i);
		e->valueint = e->valueint * 10;
		e->valuedouble = (double)e->valueint;
	}
	char *text = cJSON_Print(doc);
	cJSON_Delete(doc);
	FILE *f = fopen(path, "w");
	if(!f || !text) {
		printf("FAIL: cannot write %s\n", path);
		g_failures++;
		free(text);
		return;
	}
	fputs(text, f);
	fclose(f);
	free(text);

	CHECK(compileLayoutConfig(path, &g_cs), "compile the scaled-weights config");
	initParams();
	MacroBank mb = buildMacroBank(20, 40, 600, 320);
	CHECK(mb.dials[0]->w == 135, "the scaled weights give the same first cell (135 px)");
	CHECK(mb.dials[3]->w == 90, "the scaled weights give the same fourth cell (90 px)");
	freeGuiNode(mb.bank);

	/* Put the demo config back for whatever runs next. */
	CHECK(compileLayoutConfig(layoutPath, &g_cs), "restore the demo config");
	remove(path);
}

static int runSelfTest(const char *layoutPath, const char *themePath) {
	mkdir(".tmp_files", 0755);

	/* 1. The theme. */
	FontConfig font = { { 0 }, 0, 0 };
	memset(&g_cs, 0, sizeof(g_cs));
	loadThemeJson(themePath, &g_cs, &font);
	CHECK(g_cs.disabledText.a == 255, "the theme supplies text.disabled");
	CHECK(g_cs.fontColour.a == 255, "the theme supplies text.primary");
	CHECK(g_cs.chipPalette0.a == 255, "the theme supplies the chip palette");

	/* 2. The style config. */
	CHECK(compileLayoutConfig(layoutPath, &g_cs), "the style config compiles");
	CHECK(validateLayoutFile(layoutPath), "the style config passes the validator");
	CHECK(validateColourTokens(layoutPath), "every colour in the config is a theme token");

	/* 3. The resolved styles. */
	checkStyles();

	/* 4. The reflowed geometry. */
	initParams();
	MacroBank mb = buildMacroBank(20, 40, 600, 320);
	checkGeometry(&mb);
	freeGuiNode(mb.bank);

	/* 5. Weights are ratios, not pixels. */
	checkWeightsAreRatios(layoutPath);

	/* 6. A raw hex is not a theme token -- and a second compile replaces
	 *    the whole registry. This one runs last because it swaps the
	 *    config out from under everything else. */
	checkRawHexIsNotAToken(layoutPath);

	if(g_failures) {
		printf("%d styling-tutorial check(s) failed\n", g_failures);
		return 1;
	}
	printf("styling tutorial: all checks passed\n");
	return 0;
}

/* --- windowed mode ------------------------------------------------------ */

static void loadFonts(void) {
	/* The same fonts the app uses, when they are reachable from the
	 * current directory; otherwise raylib's built-in font. */
	pixelFont = LoadFontEx("resources/fonts/console.ttf", 9, NULL, 255);
	if(pixelFont.texture.id == 0) {
		pixelFont = GetFontDefault();
	}
	textFont = LoadFont("resources/fonts/setback.png");
	if(textFont.texture.id == 0) {
		textFont = GetFontDefault();
	}
	symbolFont = GetFontDefault();
}

static int runWindow(const char *layoutPath, const char *themePath) {
	FontConfig font = { { 0 }, 0, 0 };
	memset(&g_cs, 0, sizeof(g_cs));
	loadThemeJson(themePath, &g_cs, &font);

	SetConfigFlags(FLAG_WINDOW_RESIZABLE);
	InitWindow(640, 480, "Spectrax styling tutorial - Macro Bank");
	loadFonts();
	g_curveIcons = buildCurveIconTexture();

	if(!compileLayoutConfig(layoutPath, &g_cs)) {
		printf("FAIL: could not compile %s\n", layoutPath);
		CloseWindow();
		return 1;
	}
	/* Load the knob texture named by knob.asset. The path is resolved
	 * against the process CWD, which is why the demo chdir's into the
	 * asset directory before it starts. */
	finalizeStyles();

	initParams();
	MacroBank mb = buildMacroBank(20, 40, 600, 320);

	Graph *g = (Graph *)malloc(sizeof(Graph));
	g->root = mb.bank;
	g->selected = NULL;

	SetTargetFPS(60);
	while(!WindowShouldClose()) {
		if(IsKeyPressed(KEY_LEFT)) {
			navigateGraphRefined(g, KM_LEFT);
		}
		if(IsKeyPressed(KEY_RIGHT)) {
			navigateGraphRefined(g, KM_RIGHT);
		}
		if(IsKeyPressed(KEY_UP)) {
			navigateGraphRefined(g, KM_UP);
		}
		if(IsKeyPressed(KEY_DOWN)) {
			navigateGraphRefined(g, KM_DOWN);
		}
		BeginDrawing();
		ClearBackground(g_cs.backgroundColor);
		drawNode(mb.bank);
		DrawTextEx(pixelFont, "ARROWS: MOVE   ESC: QUIT", (Vector2){ 8, 8 }, 10, 1, g_cs.label);
		EndDrawing();
	}
	free(g);
	freeGuiNode(mb.bank);
	CloseWindow();
	return 0;
}

/* --- --dump: print the tree the layout produced ----------------------- */

static void dumpNode(const GuiNode *n, int depth) {
	for(int i = 0; i < depth; i++) {
		printf("  ");
	}
	printf("%-12s %-13s x=%3u y=%3u w=%3u h=%3u\n", n->name ? n->name : "?",
	       n->className ? n->className : "(no class)", n->x, n->y, n->w, n->h);
	if(n->items) {
		ListElement *cur = n->items->head;
		for(int i = 0; i < n->itemCount && cur; i++) {
			dumpNode(*(GuiNode **)cur->data, depth + 1);
			cur = cur->next;
		}
	}
}

static int runDump(const char *layoutPath, const char *themePath) {
	FontConfig font = { { 0 }, 0, 0 };
	memset(&g_cs, 0, sizeof(g_cs));
	loadThemeJson(themePath, &g_cs, &font);
	if(!compileLayoutConfig(layoutPath, &g_cs)) {
		printf("FAIL: could not compile %s\n", layoutPath);
		return 1;
	}
	initParams();
	MacroBank mb = buildMacroBank(20, 40, 600, 320);
	printf("%-12s %-13s %s\n", "NODE", "CLASS", "RECT");
	dumpNode(mb.bank, 0);
	freeGuiNode(mb.bank);
	return 0;
}

/* --- entry point -------------------------------------------------------- */

int main(int argc, char **argv) {
	const char *layoutPath = SPECTRAX_TUTORIAL_LAYOUT_JSON;
	const char *themePath = SPECTRAX_TUTORIAL_THEME_JSON;
	const char *assetDir = SPECTRAX_TUTORIAL_ASSET_DIR;
	bool selftest = false;
	bool dump = false;

	for(int i = 1; i < argc; i++) {
		if(strcmp(argv[i], "--selftest") == 0) {
			selftest = true;
		} else if(strcmp(argv[i], "--dump") == 0) {
			dump = true;
		} else if(strcmp(argv[i], "--layout") == 0 && i + 1 < argc) {
			layoutPath = argv[++i];
		} else if(strcmp(argv[i], "--theme") == 0 && i + 1 < argc) {
			themePath = argv[++i];
		} else if(strcmp(argv[i], "--asset-dir") == 0 && i + 1 < argc) {
			assetDir = argv[++i];
		} else {
			printf("unknown argument '%s'\n", argv[i]);
			return 2;
		}
	}

	if(selftest) {
		return runSelfTest(layoutPath, themePath);
	}
	if(dump) {
		return runDump(layoutPath, themePath);
	}

	/* knob.asset paths are resolved against the process CWD. */
	if(assetDir && assetDir[0] && chdir(assetDir) != 0) {
		printf("warning: could not enter asset directory '%s'\n", assetDir);
	}
	return runWindow(layoutPath, themePath);
}
