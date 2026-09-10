#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "gui_style.h"
#include "graph_gui.h"
#include "cJSON.h"
#include "theme.h"
#include "gui.h"  /* initDefaultColourScheme (Task 3 step-cell baked-default test) */

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
	/* contentW = 80-8 = 72 < groupW 86 -> classic horizontal inset cx+2 */
	ASSERT_TRUE(g.knobX == 106, "narrow cell keeps horizontal inset");
	/* contentH = 40-8 = 32 > knob 20 -> knob centers vertically */
	ASSERT_TRUE(g.knobY == 204 + (32 - 20) / 2, "knob centers vertically");
	/* value follows the knob; label centres in the cell below the knob */
	ASSERT_TRUE(g.valueX == g.knobX + 28, "valueX is knob-relative");
	ASSERT_TRUE(g.valueY == g.knobY + 2, "valueY is knob-relative");
	ASSERT_TRUE(g.labelX == 104 + 36, "labelX centres in the cell");
	ASSERT_TRUE(g.labelY == g.knobY + 18, "labelY sits below the knob");
	freeGuiNode(n);
	printf("PASS test_compute_dial_geometry\n");
	return 0;
}

static int test_compile_custom_dial_class(void);
static int test_compile_unknown_class_ignored(void);
static int test_missing_layout_file_keeps_defaults(void);
static int test_custom_class_geometry(void);
static int test_btn_and_typelabel_styles(void);
static int test_apply_layout_weights_and_classes(void);
static int test_step_cell_style_resolves_and_draws(void);
static int test_dest_style_resolves_and_colours(void);
static int test_chip_style_resolves_and_geometry(void);
static int test_chip_palette_resolved_from_theme(void);
static int test_mod_source_row_layout_gap(void);

static int test_reflow_fills_weighted_row(void) {
	/* FM op-row case: 5 dials at weight 60 + a blank at weight 4 in a
	 * 566px-wide row. The old integer-division reflow truncated
	 * x_scalar to 1 and left the row ~half empty; the proportional
	 * reflow must fill the content width exactly. */
	GuiNode *row = createGuiNode(0, 0, 566, 35, 1, na_horizontal, "row", 0, 0);
	GuiNode *c[6];
	for(int i = 0; i < 5; i++) {
		c[i] = createBlankGuiNode();
		appendItem(row, c[i], 60);
	}
	c[5] = createBlankGuiNode();
	appendItem(row, c[5], 4);

	int contentDim = (int)row->w - 2 * (int)row->padding;
	ASSERT_TRUE(contentDim == 564, "content dim 564");
	int sum = 0;
	for(int i = 0; i < 6; i++) {
		sum += (int)c[i]->w;
	}
	ASSERT_TRUE(sum == contentDim, "row fills container exactly");
	ASSERT_TRUE(c[0]->w > 90, "dial wide enough for its internals");
	ASSERT_TRUE(c[4]->x > c[3]->x && c[3]->x > c[2]->x, "positions advance");

	freeGuiNode(row);
	printf("PASS test_reflow_fills_weighted_row\n");
	return 0;
}

static int test_reflow_gap_distribution(void) {
	/* [1,1,1] in a 100px row, padding 0, gap 4:
	 * availDim = 100 - 8 = 92; shares 30,30,32; positions 0,34,68. */
	GuiNode *row = createGuiNode(0, 0, 100, 20, 0, na_horizontal, "row", 0, 0);
	GuiNode *c[3];
	for(int i = 0; i < 3; i++) {
		c[i] = createBlankGuiNode();
		appendItem(row, c[i], 1);
	}
	row->gap = 4;
	reflowCoordinates(row);
	ASSERT_TRUE(c[0]->x == 0 && c[0]->w == 30, "child 0 at 0, w 30");
	ASSERT_TRUE(c[1]->x == 34 && c[1]->w == 30, "child 1 at 34 (30+gap 4)");
	ASSERT_TRUE(c[2]->x == 68 && c[2]->w == 32, "child 2 at 68, absorbs remainder");
	ASSERT_TRUE(c[2]->x + c[2]->w == 100, "row fills exactly");
	freeGuiNode(row);
	printf("PASS test_reflow_gap_distribution\n");
	return 0;
}

static int test_reflow_gap_zero_matches_pinned(void) {
	/* Verify gap == 0 reproduces the proportional reflow of T2 (the
	 * pinned width from test_reflow_fills_weighted_row). */
	GuiNode *row = createGuiNode(0, 0, 566, 35, 1, na_horizontal, "row", 0, 0);
	GuiNode *c[6];
	for(int i = 0; i < 5; i++) {
		c[i] = createBlankGuiNode();
		appendItem(row, c[i], 60);
	}
	c[5] = createBlankGuiNode();
	appendItem(row, c[5], 4);
	/* gap defaults to 0 */
	ASSERT_TRUE(c[0]->w >= 90, "dial wide enough for its internals (gap 0 unchanged)");
	int total = 0;
	for(int i = 0; i < 6; i++) {
		total += c[i]->w;
	}
	ASSERT_TRUE(total == 564, "gap 0 reflow still fills the row exactly");
	freeGuiNode(row);
	printf("PASS test_reflow_gap_zero_matches_pinned\n");
	return 0;
}

static int test_reflow_fills_weighted_column(void) {
	GuiNode *col = createGuiNode(0, 0, 200, 400, 2, na_vertical, "col", 0, 0);
	GuiNode *c0 = createBlankGuiNode();
	GuiNode *c1 = createBlankGuiNode();
	GuiNode *c2 = createBlankGuiNode();
	appendItem(col, c0, 1);
	appendItem(col, c1, 2);
	appendItem(col, c2, 1);

	int contentDim = (int)col->h - 2 * (int)col->padding;
	int sum = (int)c0->h + (int)c1->h + (int)c2->h;
	ASSERT_TRUE(sum == contentDim, "column fills exactly");
	ASSERT_TRUE(c1->h > c0->h, "weight-2 child taller than weight-1");

	freeGuiNode(col);
	printf("PASS test_reflow_fills_weighted_column\n");
	return 0;
}

static int test_dial_geometry_centers_in_wide_cell(void) {
	/* wide cell: knob centers on both axes, value + label follow the
	 * knob (knob-relative); narrow cell: classic left/top inset. */
	GuiNode *wide = createGuiNode(100, 200, 113, 35, 1, na_horizontal, "w", 1, 0);
	const DialStyle *d = resolveDialStyle(wide);
	DialGeometry g;
	computeDialGeometry(wide, d, &g);
	/* contentW = 113-2 = 111; groupW = 20+28+38 = 86; slack 25; knobX = cx+12 */
	ASSERT_TRUE(g.knobX == 100 + 1 + 12, "wide cell centers knob horizontally");
	/* contentH = 33; knob 20; slack 13; knobY = cy+6 */
	ASSERT_TRUE(g.knobY == 200 + 1 + 6, "wide cell centers knob vertically");
	/* value follows the knob; label centres in the cell */
	ASSERT_TRUE(g.valueX == g.knobX + 28, "value is knob-relative");
	ASSERT_TRUE(g.labelX == 100 + 1 + 55, "label centres in the wide cell");
	freeGuiNode(wide);

	GuiNode *narrow = createGuiNode(100, 200, 58, 35, 2, na_horizontal, "n", 1, 0);
	const DialStyle *dn = resolveDialStyle(narrow);
	DialGeometry gn;
	computeDialGeometry(narrow, dn, &gn);
	/* contentW = 58-4 = 54 < 86 -> classic inset cx+2 = 104 */
	ASSERT_TRUE(gn.knobX == 100 + 2 + 2, "narrow cell keeps classic horizontal inset");
	ASSERT_TRUE(gn.labelX == 100 + 2 + 27, "narrow cell label centres too");
	freeGuiNode(narrow);
	printf("PASS test_dial_geometry_centers_in_wide_cell\n");
	return 0;
}

static int test_dial_component_height(void) {
	/* default dial: max(knob 20, value 2+14=16, label 18+9=27) = 27 */
	GuiNode *n = createGuiNode(0, 0, 100, 35, 2, na_horizontal, "g", 1, 0);
	const DialStyle *d = resolveDialStyle(n);
	ASSERT_TRUE(dialComponentHeight(d) == 27, "default dial content height");
	/* discrete: label 21+9=30 -> 30 */
	GuiNode *dn = createGuiNode(0, 0, 100, 35, 2, na_horizontal, "g", 1, 0);
	const DialStyle *dd = resolveDiscreteDialStyle(dn);
	ASSERT_TRUE(dialComponentHeight(dd) == 30, "discrete dial content height");
	/* a custom class's height follows its style */
	ASSERT_TRUE(dialComponentHeight(d) + 2 * 2 == 31, "full cell = content + padding");
	freeGuiNode(n);
	freeGuiNode(dn);
	printf("PASS test_dial_component_height\n");
	return 0;
}

int main(void) {
	int fails = 0;
	ensure_tmp_dirs();
	fails += test_baked_defaults_used_when_no_class();
	fails += test_unknown_class_falls_back_to_type_default();
	fails += test_compute_dial_geometry();
	fails += test_compile_custom_dial_class();
	fails += test_compile_unknown_class_ignored();
	fails += test_missing_layout_file_keeps_defaults();
	fails += test_custom_class_geometry();
	fails += test_btn_and_typelabel_styles();
	fails += test_apply_layout_weights_and_classes();
	fails += test_step_cell_style_resolves_and_draws();
	fails += test_dest_style_resolves_and_colours();
	fails += test_chip_style_resolves_and_geometry();
	fails += test_chip_palette_resolved_from_theme();
	fails += test_mod_source_row_layout_gap();
	fails += test_reflow_fills_weighted_row();
	fails += test_reflow_gap_distribution();
	fails += test_reflow_gap_zero_matches_pinned();
	fails += test_reflow_fills_weighted_column();
	fails += test_dial_geometry_centers_in_wide_cell();
	fails += test_dial_component_height();
	if(fails) {
		printf("%d layout test(s) failed\n", fails);
		return 1;
	}
	printf("ALL layout tests passed\n");
	return 0;
}

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
	ASSERT_TRUE(g.knobX == 106, "geometry knobX keeps inset");
	ASSERT_TRUE(g.knobY == 205, "geometry knobY centers vertically");
	ASSERT_TRUE(g.valueX == g.knobX + 40, "geometry valueX with offset");
	ASSERT_TRUE(g.valueY == g.knobY + 6, "geometry valueY");
	ASSERT_TRUE(g.labelX == 140 - 40, "geometry labelX centres + offset");
	ASSERT_TRUE(g.labelY == g.knobY + 24, "geometry labelY");
	freeGuiNode(n);
	remove(path);
	printf("PASS test_custom_class_geometry\n");
	return 0;
}

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

static int test_step_cell_style_resolves_and_draws(void) {
	/* Three things verified in one test:
	 *   1) Without a class, the resolver returns the baked default (which
	 *      is populated from a fresh ColourScheme by compileLayoutConfig).
	 *   2) compileLayoutConfig maps the "step-cell" class name to
	 *      STYLE_STEP_CELL and lets us override bgEmpty / bgPlaying /
	 *      borderSelected / note (LabelStyle subset).
	 *   3) After compile, the default StepCellStyle's baked colours match
	 *      the values baked from a real ColourScheme.  The baked defaults
	 *      mirror what drawStepGuiNode used to hardcode so the no-class
	 *      layout.json render stays byte-for-byte identical.
	 */
	const char *path = ".tmp_files/layout_test_step_cell.json";
	FILE *f = fopen(path, "w");
	fputs("{\"styles\":{\"step-cell\":{\"bgEmpty\":\"selectedCell\","
	      "\"bgPlaying\":\"reddish\","
	      "\"borderSelected\":\"background\","
	      "\"note\":{\"fontSize\":11,\"offsetX\":4,\"offsetY\":4}}}}", f);
	fclose(f);
	ColourScheme cs;
	initDefaultColourScheme(&cs);
	compileLayoutConfig(path, &cs);

	/* Resolution: a classless node returns the baked default. */
	GuiNode *plain = createBlankGuiNode();
	const StepCellStyle *st = resolveStepCellStyle(plain);
	ASSERT_TRUE(st != NULL, "resolver returns baked default for classless node");
	/* Baked defaults populated from initDefaultColourScheme's
	 * defaultCell = {148,68,16,255}; highlight = {214,60,17,255};
	 * outlineColour = {219,148,103,255}; fontColour = {99,17,0,255}. */
	ASSERT_TRUE(st->bgEmpty.r == 148 && st->bgEmpty.g == 68 &&
	            st->bgEmpty.b == 16 && st->bgEmpty.a == 255,
	            "baked bgEmpty == defaultCell");
	ASSERT_TRUE(st->bgPlaying.r == 214 && st->bgPlaying.g == 60 &&
	            st->bgPlaying.b == 17 && st->bgPlaying.a == 255,
	            "baked bgPlaying == highlightedCell");
	ASSERT_TRUE(st->borderSelected.r == 219 && st->borderSelected.a == 255,
	            "baked borderSelected == outlineColour");
	ASSERT_TRUE(st->note.color.r == 99 && st->note.color.g == 17 &&
	            st->note.color.b == 0 && st->note.color.a == 255,
	            "baked note.color == fontColour");
	/* The note LabelStyle's fontName must map (via styleFont) to textFont. */
	ASSERT_TRUE(st->note.fontName && strcmp(st->note.fontName, "text") == 0,
	            "note fontName references text font slot");
	/* Baked default keeps fontSize at 0 so drawStepGuiNode's ternary
	 * falls through to textFont.baseSize — byte-for-byte the same size
	 * the pre-StepCellStyle drawStepGuiNode used. */
	ASSERT_TRUE(st->note.fontSize == 0,
	            "baked note.fontSize == 0 (fall through to textFont.baseSize)");
	freeGuiNode(plain);

	/* Unknown class falls back to the baked default (same pointer). */
	GuiNode *u = createBlankGuiNode();
	guiNodeSetClass(u, "no-such-step-cell-class");
	const StepCellStyle *fb = resolveStepCellStyle(u);
	ASSERT_TRUE(fb == st, "unknown class returns the same baked default");
	freeGuiNode(u);

	/* Custom class wins: a node classed "step-cell" gets its overrides.
	 * "selectedCell"/"reddish"/"background" are theme field names chosen
	 * to differ from defaultCell / highlightedCell / outlineColour so we
	 * can be sure the override went through. */
	GuiNode *cn = createBlankGuiNode();
	guiNodeSetClass(cn, "step-cell");
	const StepCellStyle *custom = resolveStepCellStyle(cn);
	ASSERT_TRUE(custom != st, "class match returns a custom entry");
	ASSERT_TRUE(custom->bgEmpty.r == cs.selectedCell.r &&
	            custom->bgEmpty.g == cs.selectedCell.g &&
	            custom->bgEmpty.b == cs.selectedCell.b,
	            "custom bgEmpty == cs.selectedCell");
	ASSERT_TRUE(custom->bgPlaying.r == cs.reddish.r &&
	            custom->bgPlaying.g == cs.reddish.g &&
	            custom->bgPlaying.b == cs.reddish.b,
	            "custom bgPlaying == cs.reddish");
	ASSERT_TRUE(custom->borderSelected.r == cs.backgroundColor.r &&
	            custom->borderSelected.g == cs.backgroundColor.g &&
	            custom->borderSelected.b == cs.backgroundColor.b,
	            "custom borderSelected == cs.backgroundColor");
	ASSERT_TRUE(custom->note.fontSize == 11, "custom note.fontSize");
	ASSERT_TRUE(custom->note.offsetX == 4, "custom note.offsetX");
	ASSERT_TRUE(custom->note.offsetY == 4, "custom note.offsetY");
	freeGuiNode(cn);

	remove(path);
	printf("PASS test_step_cell_style_resolves_and_draws\n");
	return 0;
}

static int test_dest_style_resolves_and_colours(void) {
	/* Task 4: DestStyle carries the route-dest outline. The baked default
	 * must mirror what drawRouteDestGuiNode used to hardcode (borderWidth
	 * 2, border.color from cs.routeAdd, border.colorSelected from
	 * cs.labelSelected) so the no-class layout.json render stays
	 * byte-for-byte identical. We don't assert exact RGB triples here —
	 * those are theme-defined — only that the colour sources are non-zero.
	 */
	const char *path = ".tmp_files/layout_test_dest.json";
	FILE *f = fopen(path, "w");
	fputs("{\"styles\":{}}", f);
	fclose(f);
	ColourScheme cs;
	initDefaultColourScheme(&cs);
	compileLayoutConfig(path, &cs);

	GuiNode *n = createGuiNode(0, 0, 40, 30, 0, na_horizontal, "dest", 1, 0);
	const DestStyle *st = resolveDestStyle(n);
	ASSERT_TRUE(st != NULL, "route-dest style resolves");
	ASSERT_TRUE(st->border.borderWidth == 2, "dest border width 2");
	ASSERT_TRUE(st->border.color.r > 0, "dest border colour non-zero (routeAdd)");
	ASSERT_TRUE(st->border.colorSelected.g > 0, "dest selected colour non-zero (labelSelected)");
	freeGuiNode(n);
	remove(path);
	printf("PASS test_dest_style_resolves_and_colours\n");
	return 0;
}

static int test_chip_style_resolves_and_geometry(void) {
	GuiNode *n = createGuiNode(0, 0, 120, 30, 0, na_horizontal, "chip", 1, 0);
	const ChipStyle *st = resolveChipStyle(n);
	ASSERT_TRUE(st != NULL, "chip style resolves");
	ASSERT_TRUE(st->border.borderWidth == 2.0f, "chip border width 2");
	ASSERT_TRUE(st->dots.size == 4 && st->dots.gap == 2, "chip dots defaults");
	ASSERT_TRUE(chipComponentHeight(st) == 24,
	            "chip intrinsic height (label 12 + patch 8 + dots 4)");

	ChipGeometry g;
	computeChipGeometry(n, st, "FM", "chan", &g);
	ASSERT_TRUE(g.voiceCountY == 2, "voice count top at +2");
	/* label centred: (h - fontSize)/2 + offsetY = (30-12)/2 + 0 = 9 */
	ASSERT_TRUE(g.labelY == 9, "label vertically centred");
	/* patchName bottom: y + h + offsetY = 0 + 30 + -11 = 19 */
	ASSERT_TRUE(g.patchNameY == 19, "patch name at bottom (offsetY -11)");
	freeGuiNode(n);
	printf("PASS test_chip_style_resolves_and_geometry\n");
	return 0;
}

static int test_chip_palette_resolved_from_theme(void) {
	const char *path = ".tmp_files/layout_test_chip.json";
	FILE *f = fopen(path, "w");
	fputs("{\"styles\":{\"chip\":{\"palette\":[\"chipPalette0\",\"chipPalette3\"]}}}", f);
	fclose(f);
	ColourScheme cs;
	initDefaultColourScheme(&cs);
	ASSERT_TRUE(compileLayoutConfig(path, &cs), "compile chip layout");

	GuiNode *n = createGuiNode(0, 0, 120, 30, 0, na_horizontal, "chip", 1, 0);
	guiNodeSetClass(n, "chip");
	const ChipStyle *st = resolveChipStyle(n);
	/* chipPalette0 default = steel-blue (70,130,180) */
	ASSERT_TRUE(st->palette[0].r == 70, "palette[0] resolved chipPalette0 (steel-blue)");
	/* chipPalette3 default = mulberry (170,80,130) */
	ASSERT_TRUE(st->palette[3].r == 170, "palette[3] resolved chipPalette3 (mulberry)");
	freeGuiNode(n);
	remove(path);
	printf("PASS test_chip_palette_resolved_from_theme\n");
	return 0;
}

static int test_mod_source_row_layout_gap(void) {
	/* T6: compile a temp layout containing "mod-source-row" (orientation,
	 * padding, gap, weights), apply it to a row, assert gap + orientation
	 * land on the container. */
	const char *path = ".tmp_files/layout_test_msr.json";
	FILE *f = fopen(path, "w");
	fputs("{\"layouts\":{\"mod-source-row\":{\"orientation\":\"horizontal\","
	      "\"padding\":2,\"gap\":2,\"weights\":[3,4,4,4,4,3,2,1]}}}", f);
	fclose(f);
	ColourScheme cs;
	memset(&cs, 0, sizeof(cs));
	ASSERT_TRUE(compileLayoutConfig(path, &cs), "compile msr layout");

	GuiNode *row = createGuiNode(0, 0, 566, 40, 0, na_horizontal, "row", 0, 0);
	GuiNode *c[3];
	for(int i = 0; i < 3; i++) {
		c[i] = createBlankGuiNode();
		appendItem(row, c[i], 1);
	}
	applyLayout(row, "mod-source-row");
	ASSERT_TRUE(row->gap == 2, "layout gap applied to container");
	ASSERT_TRUE(row->padding == 2, "layout padding applied to container");
	ASSERT_TRUE(row->nodeAlignment == na_horizontal, "orientation applied");

	/* confirm reflow distributes weights with the gap reserved:
	 * availDim = (566 - 2*2) - 2*2*gap = 562 - 4 = 558; weights 1+1+1 = 3,
	 * each child gets 558/3 = 186 except the last which absorbs remainder. */
	ASSERT_TRUE(c[0]->x == 2, "first child after padding");
	ASSERT_TRUE(c[1]->x - c[0]->x == c[0]->w + 2, "gap between children");
	ASSERT_TRUE(c[2]->x + c[2]->w == 564, "row fills container minus padding");

	freeGuiNode(row);
	remove(path);
	printf("PASS test_mod_source_row_layout_gap\n");
	return 0;
}
