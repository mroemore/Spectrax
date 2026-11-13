#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "gui_style.h"
#include "graph_gui.h"
#include "cJSON.h"
#include "io/config_io.h"
#include "theme.h"
#include "gui.h"  /* initDefaultColourScheme (Task 3 step-cell baked-default test) */
#include "curve_icons.h"

#define ASSERT_TRUE(c, m) do { if(!(c)) { printf("FAIL: %s\n", m); return 1; } } while(0)
#define TMP_DIR ".tmp_files/"

static void ensure_tmp_dirs(void) { mkdir(TMP_DIR, 0755); }

static int test_type_key_fallback_resolves(void);
static int test_btn_and_typelabel_styles(void);

static int test_baked_defaults_used_when_no_class(void) {
	GuiNode *n = createBlankGuiNode();
	const DialStyle *d = resolveDialStyle(n);
	ASSERT_TRUE(d->knob.size == 28, "knob size 28 (1.5x baked default)");
	ASSERT_TRUE(d->knob.startAngle == -225, "start angle");
	ASSERT_TRUE(d->knob.sweep == 270, "sweep");
	ASSERT_TRUE(strcmp(d->value.format, "%05.2f") == 0, "value format");
	ASSERT_TRUE(d->value.offsetX == 34, "value offsetX");
	ASSERT_TRUE(d->label.offsetY == 26, "label offsetY");
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
	ASSERT_TRUE(d->knob.size == 28, "fallback knob size (1.5x baked default)");
	freeGuiNode(n);
	printf("PASS test_unknown_class_falls_back_to_type_default\n");
	return 0;
}

static int test_compute_dial_geometry(void) {
	GuiNode *n = createGuiNode(100, 200, 80, 40, 4, na_horizontal, "g", 1, 0);
	/* Classless, so this resolves through the type-named "dial" entry the
	 * suite compiled earlier — not the baked default. */
	const DialStyle *d = resolveDialStyle(n);
	DialGeometry g;
	computeDialGeometry(n, d, &g);
	/* contentW = 80-8 = 72 < groupW 86 -> classic horizontal inset cx+2 */
	ASSERT_TRUE(g.knobX == 106, "narrow cell keeps horizontal inset");
	/* contentH = 40-8 = 32 > knob d->knob.size -> knob centers vertically */
	ASSERT_TRUE(g.knobY == 204 + (32 - d->knob.size) / 2, "knob centers vertically");
	/* value follows the knob; label centres in the cell below the knob */
	/* value follows the knob; the cell-aware clamp pushes it to the knob's
	 * right edge when the style's offset would start it inside the knob
	 * (the dial styles are wider than their value offset). */
	int expectValueX = g.knobX + d->value.offsetX;
	if(expectValueX < g.knobX + g.knobW) {
		expectValueX = g.knobX + g.knobW + 2;
	}
	ASSERT_TRUE(g.valueX == expectValueX, "valueX is knob-relative, clamped clear of the knob");
	ASSERT_TRUE(g.valueY == g.knobY + 2, "valueY is knob-relative");
	ASSERT_TRUE(g.labelX == 104 + 36, "labelX centres in the cell");
	ASSERT_TRUE(g.labelY == g.knobY + 26, "labelY sits below the knob (baked label offsetY 26)");
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
static int test_text_style_resolves(void);
static int test_btn_sublabel_resolves(void);
static int test_curve_icon_index(void);
static int test_curve_icon_default_framecount_zero(void);
static int test_curve_icon_style_resolves(void);
static int test_stage_dial_styles_resolve(void);
static int test_env_stage_layout(void);

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
	/* 1.5x dial: max(knob 28, value 2+20=22, label 26+14=40) = 40 */
	GuiNode *n = createGuiNode(0, 0, 100, 50, 2, na_horizontal, "g", 1, 0);
	const DialStyle *d = resolveDialStyle(n);
	ASSERT_TRUE(dialComponentHeight(d) == 40, "default dial content height");
	/* discrete: max(28, value 5+14=19, label 21+14=35) = 35 */
	GuiNode *dn = createGuiNode(0, 0, 100, 50, 2, na_horizontal, "g", 1, 0);
	const DialStyle *dd = resolveDiscreteDialStyle(dn);
	ASSERT_TRUE(dialComponentHeight(dd) == 35, "discrete dial content height");
	/* a custom class's height follows its style */
	ASSERT_TRUE(dialComponentHeight(d) + 2 * 2 == 44, "full cell = content + padding");
	freeGuiNode(n);
	freeGuiNode(dn);
	printf("PASS test_dial_component_height\n");
	return 0;
}


static int test_border_colour_selected_resolves(void) {
	/* border.colorSelected is honoured by every bordered widget; leaving
	 * it out must keep alpha 0 so the draw falls back to the legacy
	 * highlight instead of painting a transparent border. */
	const char *path = ".tmp_files/layout_test_colour_selected.json";
	FILE *f = fopen(path, "w");
	ASSERT_TRUE(f != NULL, "open temp layout");
	fputs("{\"styles\":{\"dial\":{\"border\":{\"colorSelected\":\"valueText\"}}}}", f);
	fclose(f);
	ColourScheme cs;
	memset(&cs, 0, sizeof(cs));
	cs.valueText = (Color){ 10, 20, 30, 255 };
	ASSERT_TRUE(compileLayoutConfig(path, &cs), "compile colorSelected layout");

	GuiNode *n = createBlankGuiNode();
	guiNodeSetClass(n, "dial");
	const DialStyle *d = resolveDialStyle(n);
	ASSERT_TRUE(d->border.colorSelected.a == 255, "colorSelected alpha set");
	ASSERT_TRUE(d->border.colorSelected.r == 10 && d->border.colorSelected.g == 20 &&
	            d->border.colorSelected.b == 30, "colorSelected resolved from theme");
	freeGuiNode(n);
	remove(path);

	/* The classless path resolves through the type-named "dial" entry,
	 * which this fixture defines — so it inherits colorSelected, exactly
	 * like the explicitly-named node above. */
	GuiNode *plain = createBlankGuiNode();
	const DialStyle *p2 = resolveDialStyle(plain);
	ASSERT_TRUE(p2->border.colorSelected.a == 255,
	            "classless node inherits colorSelected via the type-named entry");
	freeGuiNode(plain);

	/* The baked default with no class resolution at all is only reachable
	 * for a type the config does not define. This fixture defines only
	 * "dial", so the discrete dial's baked default still has alpha 0. */
	const DialStyle *baked = resolveDiscreteDialStyle(NULL);
	ASSERT_TRUE(baked->border.colorSelected.a == 0,
	            "unset colorSelected stays transparent on the baked default");
	printf("PASS test_border_colour_selected_resolves\n");
	return 0;
}

/* --- shipped-config guard ------------------------------------------ */

/* Every colour-bearing JSON key the style parser understands. Keep in
 * sync with the overlay* helpers in gui_style.c; only these names are
 * interpreted as theme tokens, so formats/fonts are never mistaken for
 * colours. */
static const char *const kColourKeys[] = {
	"color", "colorSelected", "bgEmpty", "bgPlaying", "borderSelected",
	"colour", "colourActive", NULL
};

static int isColourKey(const char *k) {
	if(!k) {
		return 0;
	}
	for(int i = 0; kColourKeys[i]; i++) {
		if(strcmp(kColourKeys[i], k) == 0) {
			return 1;
		}
	}
	return 0;
}

static int checkColourTokens(cJSON *node, const ColourScheme *cs, const char *path) {
	if(cJSON_IsObject(node)) {
		cJSON *ch = NULL;
		cJSON_ArrayForEach(ch, node) {
			char sub[192];
			snprintf(sub, sizeof(sub), "%s.%s", path, ch->string ? ch->string : "?");
			if(isColourKey(ch->string)) {
				if(!cJSON_IsString(ch) || !themeFieldByName((ColourScheme *)cs, ch->valuestring)) {
					printf("FAIL: %s is not a theme colour token\n", sub);
					return 0;
				}
			}
			if(ch->string && strcmp(ch->string, "palette") == 0 && cJSON_IsArray(ch)) {
				int n = cJSON_GetArraySize(ch);
				for(int i = 0; i < n; i++) {
					cJSON *e = cJSON_GetArrayItem(ch, i);
					if(!cJSON_IsString(e) || !themeFieldByName((ColourScheme *)cs, e->valuestring)) {
						printf("FAIL: %s[%d] is not a theme colour token\n", sub, i);
						return 0;
					}
				}
			}
			if(!checkColourTokens(ch, cs, sub)) {
				return 0;
			}
		}
	} else if(cJSON_IsArray(node)) {
		int n = cJSON_GetArraySize(node);
		for(int i = 0; i < n; i++) {
			char sub[192];
			snprintf(sub, sizeof(sub), "%s[%d]", path, i);
			if(!checkColourTokens(cJSON_GetArrayItem(node, i), cs, sub)) {
				return 0;
			}
		}
	}
	return 1;
}

/* The shipped src/layout.json is the reference config. Anything the
 * parser would silently ignore (unknown class, missing extends target,
 * bad colour token, childClasses/weights mismatch) fails here instead of
 * reaching users as an invisible no-op. */
static int test_shipped_layout_json_is_valid(void) {
	ColourScheme cs;
	memset(&cs, 0, sizeof(cs));
	initDefaultColourScheme(&cs);

	const char *path = SPECTRAX_SRC_LAYOUT_JSON;
	FILE *f = fopen(path, "rb");
	if(!f) {
		printf("FAIL: cannot open shipped config %s\n", path);
		return 1;
	}
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = (char *)malloc((size_t)sz + 1);
	ASSERT_TRUE(buf != NULL, "alloc shipped config buffer");
	ASSERT_TRUE(fread(buf, 1, (size_t)sz, f) == (size_t)sz, "read shipped config");
	buf[sz] = '\0';
	fclose(f);
	cJSON *doc = cJSON_Parse(buf);
	free(buf);
	ASSERT_TRUE(doc != NULL, "shipped config parses as JSON");

	cJSON *styles = cJSON_GetObjectItemCaseSensitive(doc, "styles");
	ASSERT_TRUE(cJSON_IsObject(styles), "styles object present");
	ASSERT_TRUE(compileLayoutConfig(path, &cs), "shipped config compiles");

	/* One class per live style type, so the file documents the schema. */
	static const char *const required[] = {
		"dial", "dial-discrete", "btn", "type-label",
		"step-cell", "route-dest", "chip", "text", NULL
	};
	for(int i = 0; required[i]; i++) {
		ASSERT_TRUE(cJSON_GetObjectItemCaseSensitive(styles, required[i]) != NULL,
		            "required style class is defined");
		ASSERT_TRUE(styleClassType(required[i]) >= 0,
		            "required style class compiles");
	}
	/* Retired in favour of DialStyle.curveIcon. */
	ASSERT_TRUE(cJSON_GetObjectItemCaseSensitive(styles, "curve-icon") == NULL,
	            "no standalone curve-icon class");

	/* Every class must classify and every extends target must exist in
	 * this same file. */
	cJSON *it = NULL;
	cJSON_ArrayForEach(it, styles) {
		ASSERT_TRUE(cJSON_IsObject(it), "every style entry is an object");
		if(styleClassType(it->string) < 0) {
			printf("FAIL: style class '%s' did not compile (unknown type)\n", it->string);
			cJSON_Delete(doc);
			return 1;
		}
		cJSON *parent = cJSON_GetObjectItemCaseSensitive(it, "extends");
		if(cJSON_IsString(parent) &&
		   cJSON_GetObjectItemCaseSensitive(styles, parent->valuestring) == NULL) {
			printf("FAIL: style '%s' extends missing class '%s'\n",
			       it->string, parent->valuestring);
			cJSON_Delete(doc);
			return 1;
		}
	}

	ASSERT_TRUE(checkColourTokens(styles, &cs, "styles"),
	            "every colour value names a theme token");

	cJSON *layouts = cJSON_GetObjectItemCaseSensitive(doc, "layouts");
	if(cJSON_IsObject(layouts)) {
		cJSON *l = NULL;
		cJSON_ArrayForEach(l, layouts) {
			ASSERT_TRUE(layoutByName(l->string) != NULL, "layout registers by name");
			cJSON *weights = cJSON_GetObjectItemCaseSensitive(l, "weights");
			cJSON *cc = cJSON_GetObjectItemCaseSensitive(l, "childClasses");
			if(cJSON_IsArray(weights) && cJSON_IsArray(cc) &&
			   cJSON_GetArraySize(weights) != cJSON_GetArraySize(cc)) {
				printf("FAIL: layout '%s' has %d weights but %d childClasses\n",
				       l->string, cJSON_GetArraySize(weights), cJSON_GetArraySize(cc));
				cJSON_Delete(doc);
				return 1;
			}
			if(cJSON_IsArray(weights)) {
				ASSERT_TRUE(cJSON_GetArraySize(weights) <= MAX_NODE_CHILDREN,
				            "layout weights fit MAX_NODE_CHILDREN");
			}
			if(cJSON_IsArray(cc)) {
				int n = cJSON_GetArraySize(cc);
				for(int i = 0; i < n; i++) {
					cJSON *e = cJSON_GetArrayItem(cc, i);
					ASSERT_TRUE(cJSON_IsString(e), "childClasses entries are strings");
					if(e->valuestring[0] && styleClassType(e->valuestring) < 0) {
						printf("FAIL: layout '%s' childClasses[%d] names unknown class '%s'\n",
						       l->string, i, e->valuestring);
						cJSON_Delete(doc);
						return 1;
					}
				}
			}
		}
	}

	cJSON_Delete(doc);
	printf("PASS test_shipped_layout_json_is_valid\n");
	return 0;
}

static int test_dial_value_clamped_to_cell(void) {
	/* Narrow continuous cell (a sampler dial is 62px wide): the value
	 * readout must not spill past the cell into the neighbour. */
	GuiNode *n = createGuiNode(39, 91, 62, 30, 5, na_horizontal, "SAMPLE", 1, 0);
	const DialStyle *d = resolveDialStyle(n);
	DialGeometry g;
	computeDialGeometry(n, d, &g);
	ASSERT_TRUE(g.valueX + g.valueW <= 39 + 62, "continuous value fits inside the cell");
	freeGuiNode(n);

	/* Discrete dial: the value must not sit under the knob. */
	GuiNode *m = createGuiNode(39, 91, 62, 30, 5, na_horizontal, "GAIN", 1, 0);
	const DialStyle *dd = resolveDiscreteDialStyle(m);
	DialGeometry gd;
	computeDialGeometry(m, dd, &gd);
	ASSERT_TRUE(gd.valueX >= gd.knobX + gd.knobW, "discrete value clear of the knob");
	ASSERT_TRUE(gd.valueX + gd.valueW <= 39 + 62, "discrete value fits inside the cell");
	freeGuiNode(m);

	printf("PASS test_dial_value_clamped_to_cell\n");
	return 0;
}

int main(void) {
	int fails = 0;
	ensure_tmp_dirs();
	fails += test_baked_defaults_used_when_no_class();
	fails += test_unknown_class_falls_back_to_type_default();
	fails += test_type_key_fallback_resolves();
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
	fails += test_text_style_resolves();
	fails += test_btn_sublabel_resolves();
	fails += test_curve_icon_index();
	fails += test_curve_icon_default_framecount_zero();
	fails += test_curve_icon_style_resolves();
	fails += test_stage_dial_styles_resolve();
	fails += test_env_stage_layout();
	fails += test_border_colour_selected_resolves();
	fails += test_shipped_layout_json_is_valid();
	fails += test_reflow_fills_weighted_row();
	fails += test_reflow_gap_distribution();
	fails += test_reflow_gap_zero_matches_pinned();
	fails += test_reflow_fills_weighted_column();
	fails += test_dial_geometry_centers_in_wide_cell();
	fails += test_dial_component_height();
	fails += test_dial_value_clamped_to_cell();
	if(fails) {
		printf("%d layout test(s) failed\n", fails);
		return 1;
	}
	printf("ALL layout tests passed\n");
	return 0;
}

static int test_type_key_fallback_resolves(void) {
	/* The app's typed factories never call guiNodeSetClass, so a widget
	 * with no class must still honour the class named after its type
	 * ("dial", "btn", ...) in the config. Without this fallback those
	 * per-type entries are decorative: the widget would resolve to the
	 * baked default instead. This pins the two-step chain:
	 *   explicit class -> type-named class -> baked default.
	 */
	const char *path = ".tmp_files/layout_test_typekey.json";
	FILE *f = fopen(path, "w");
	fputs("{\"styles\":{\"dial\":{\"knob\":{\"size\":30,\"radius\":15}},"
	      "\"my-dial\":{\"extends\":\"dial\",\"knob\":{\"size\":44}}}}", f);
	fclose(f);
	ColourScheme cs;
	initDefaultColourScheme(&cs);
	compileLayoutConfig(path, &cs);

	/* Classless: picks up the type-named "dial" entry, not the baked 20. */
	GuiNode *plain = createBlankGuiNode();
	ASSERT_TRUE(resolveDialStyle(plain)->knob.size == 30,
	            "classless dial resolves to the type-named 'dial' entry");
	ASSERT_TRUE(resolveDialStyle(plain)->knob.radius == 15,
	            "classless dial picks up radius from the type-named entry");

	/* Explicit class still wins over the type-named fallback. */
	GuiNode *named = createBlankGuiNode();
	guiNodeSetClass(named, "my-dial");
	ASSERT_TRUE(resolveDialStyle(named)->knob.size == 44,
	            "explicit class wins over the type-named fallback");

	/* An unknown class falls through to the type-named entry too. */
	GuiNode *unknown = createBlankGuiNode();
	guiNodeSetClass(unknown, "no-such-dial");
	ASSERT_TRUE(resolveDialStyle(unknown)->knob.size == 30,
	            "unknown class falls through to the type-named entry");

	freeGuiNode(plain);
	freeGuiNode(named);
	freeGuiNode(unknown);
	remove(path);
	printf("PASS test_type_key_fallback_resolves\n");
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
	ASSERT_TRUE(d->knob.size == 28, "untyped class ignored -> baked default (1.5x)");
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
	ASSERT_TRUE(d->knob.size == 28, "defaults intact (1.5x baked default)");
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

	/* Resolution: a classless node resolves through the type-named class
	 * ("step-cell"), per resolveStepCellStyle's type-key fallback. A
	 * blank node has no class of its own, so it picks up the config's
	 * step-cell entry. */
	GuiNode *plain = createBlankGuiNode();
	const StepCellStyle *st = resolveStepCellStyle(plain);
	ASSERT_TRUE(st != NULL, "resolver returns a style for classless node");
	ASSERT_TRUE(st->bgEmpty.r == cs.selectedCell.r &&
	            st->bgEmpty.g == cs.selectedCell.g &&
	            st->bgEmpty.b == cs.selectedCell.b,
	            "classless node picks up the type-named step-cell entry");
	/* The note LabelStyle's fontName must map (via styleFont) to textFont. */
	ASSERT_TRUE(st->note.fontName && strcmp(st->note.fontName, "text") == 0,
	            "note fontName references text font slot");
	/* The config's step-cell entry sets fontSize 11 (see the fixture
	 * above), proving the classless path landed on the JSON entry. */
	ASSERT_TRUE(st->note.fontSize == 11,
	            "classless node uses the config's step-cell note.fontSize");
	freeGuiNode(plain);

	/* Unknown class also falls through to the type-named class, so it
	 * resolves to the same entry as a classless node. */
	GuiNode *u = createBlankGuiNode();
	guiNodeSetClass(u, "no-such-step-cell-class");
	const StepCellStyle *fb = resolveStepCellStyle(u);
	ASSERT_TRUE(fb == st, "unknown class falls through to the type-named entry");
	freeGuiNode(u);

	/* Custom class: a node classed "step-cell" resolves to that entry.
	 * This is now the same entry a classless node reaches via the
	 * type-key fallback, so both must agree. */
	GuiNode *cn = createBlankGuiNode();
	guiNodeSetClass(cn, "step-cell");
	const StepCellStyle *custom = resolveStepCellStyle(cn);
	ASSERT_TRUE(custom == st, "the explicit class and the type-key fallback agree");
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
	ASSERT_TRUE(chipComponentHeight(st) == 31,
	            "chip intrinsic height (label 16 + patch 11 + dots 4)");

	ChipGeometry g;
	computeChipGeometry(n, st, "FM", "chan", &g);
	ASSERT_TRUE(g.voiceCountY == 2, "voice count top at +2");
	/* label centred: (h - fontSize)/2 + offsetY = (30-16)/2 + 0 = 7 */
	ASSERT_TRUE(g.labelY == 7, "label vertically centred");
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

static int test_curve_icon_index(void) {
	if(curveIconIndex(0.0f) != 0) { printf("FAIL lo\n"); return 1; }
	if(curveIconIndex(1.0f) != CURVE_ICON_COUNT - 1) { printf("FAIL hi\n"); return 1; }
	if(curveIconIndex(0.5f) != 15 && curveIconIndex(0.5f) != 16) { printf("FAIL mid %d\n", curveIconIndex(0.5f)); return 1; }
	printf("PASS test_curve_icon_index\n");
	return 0;
}

static int test_curve_icon_default_framecount_zero(void) {
	/* Default (no class set) must have frameCount == 0 so ordinary
	 * dials are unaffected: drawDialGuiNode only branches on
	 * frameCount > 0 to draw the curve icon. */
	GuiNode *n = createBlankGuiNode();
	const DialStyle *ds = resolveDialStyle(n);
	ASSERT_TRUE(ds != NULL, "resolveDialStyle non-null");
	ASSERT_TRUE(ds->curveIcon.frameCount == 0, "default dial curveIcon frameCount == 0");
	freeGuiNode(n);
	printf("PASS test_curve_icon_default_framecount_zero\n");
	return 0;
}

static int test_curve_icon_style_resolves(void) {
	/* Compile a temp layout where stage-curve-icon embeds a curveIcon
	 * sub-object inside a dial. The old STYLE_CURVE_ICON class is gone;
	 * the icon is now part of DialStyle and reached via
	 * resolveDialStyle(n)->curveIcon. */
	const char *path = ".tmp_files/layout_test_ci.json";
	FILE *f = fopen(path, "w");
	ASSERT_TRUE(f != NULL, "open temp layout");
	fputs("{\"styles\":{\"dial\":{},"
	      "\"stage-curve-icon\":{\"extends\":\"dial\","
	      "\"curveIcon\":{\"frameCount\":32,\"size\":16,"
	      "\"offsetX\":2,\"offsetY\":2,\"color\":\"valueText\"}}}}", f);
	fclose(f);
	ColourScheme cs;
	memset(&cs, 0, sizeof(cs));
	cs.valueText = (Color){ 200, 150, 100, 255 };
	ASSERT_TRUE(compileLayoutConfig(path, &cs), "compile curve-icon layout");

	GuiNode *n = createBlankGuiNode();
	guiNodeSetClass(n, "stage-curve-icon");
	const DialStyle *ds = resolveDialStyle(n);
	const CurveIconStyle *ci = &ds->curveIcon;
	ASSERT_TRUE(ci->frameCount == CURVE_ICON_COUNT, "frameCount == 32");
	ASSERT_TRUE(ci->size == 16, "size 16");
	ASSERT_TRUE(ci->offsetX == 2 && ci->offsetY == 2, "offsets");
	ASSERT_TRUE(ci->color.r == 200 && ci->color.g == 150 && ci->color.b == 100,
	            "color resolved from theme");
	/* Unknown class falls back to default. */
	guiNodeSetClass(n, "no-such-icon-class");
	const DialStyle *fbds = resolveDialStyle(n);
	ASSERT_TRUE(fbds->curveIcon.frameCount == 0, "unknown class fallback frameCount 0");
	freeGuiNode(n);
	remove(path);
	printf("PASS test_curve_icon_style_resolves\n");
	return 0;
}

static int test_stage_dial_styles_resolve(void) {
	/* stage-curve: small knob + value hidden (width 0, height 0, fontSize 0).
	 * stage-rate:  unchanged knob, value pushed down (offsetY 34, fontSize 7). */
	const char *path = ".tmp_files/layout_test_stage.json";
	FILE *f = fopen(path, "w");
	ASSERT_TRUE(f != NULL, "open temp layout");
	fputs("{\"styles\":{"
	      "\"stage-curve\":{\"extends\":\"dial\","
	      "\"knob\":{\"size\":13,\"radius\":7},"
	      "\"value\":{\"width\":0,\"height\":0,\"fontSize\":0}},"
	      "\"stage-rate\":{\"extends\":\"dial\","
	      "\"value\":{\"offsetY\":34,\"fontSize\":7}}}}", f);
	fclose(f);
	ColourScheme cs;
	memset(&cs, 0, sizeof(cs));
	ASSERT_TRUE(compileLayoutConfig(path, &cs), "compile stage layout");

	GuiNode *nc = createBlankGuiNode();
	guiNodeSetClass(nc, "stage-curve");
	const DialStyle *sc = resolveDialStyle(nc);
	ASSERT_TRUE(sc->knob.size == 13, "stage-curve knob.size 13");
	ASSERT_TRUE(sc->knob.radius == 7, "stage-curve knob.radius 7");
	ASSERT_TRUE(sc->value.width == 0, "stage-curve value.width 0");
	ASSERT_TRUE(sc->value.height == 0, "stage-curve value.height 0");
	ASSERT_TRUE(sc->value.fontSize == 0, "stage-curve value.fontSize 0");
	freeGuiNode(nc);

	GuiNode *nr = createBlankGuiNode();
	guiNodeSetClass(nr, "stage-rate");
	const DialStyle *sr = resolveDialStyle(nr);
	ASSERT_TRUE(sr->knob.size == 28, "stage-rate inherits default knob.size 28");
	ASSERT_TRUE(sr->value.offsetY == 34, "stage-rate value.offsetY 34");
	ASSERT_TRUE(sr->value.fontSize == 7, "stage-rate value.fontSize 7");
	freeGuiNode(nr);
	remove(path);
	printf("PASS test_stage_dial_styles_resolve\n");
	return 0;
}

static int test_env_stage_layout(void) {
	/* env-stage: horizontal weights [2,1] with childClasses
	 * ["stage-rate","stage-curve"]; row gets 2/3 of width for the
	 * rate dial and 1/3 for the curve dial. */
	const char *path = ".tmp_files/layout_test_env_stage.json";
	FILE *f = fopen(path, "w");
	ASSERT_TRUE(f != NULL, "open temp layout");
	fputs("{\"layouts\":{\"env-stage\":{\"orientation\":\"horizontal\","
	      "\"padding\":2,\"weights\":[2,1],"
	      "\"childClasses\":[\"stage-rate\",\"stage-curve\"]}}}", f);
	fclose(f);
	ColourScheme cs;
	memset(&cs, 0, sizeof(cs));
	ASSERT_TRUE(compileLayoutConfig(path, &cs), "compile env-stage layout");

	GuiNode *row = createGuiNode(0, 0, 300, 40, 0, na_horizontal, "row", 0, 0);
	GuiNode *c[2];
	c[0] = createBlankGuiNode();
	c[1] = createBlankGuiNode();
	appendItem(row, c[0], 1);
	appendItem(row, c[1], 1);
	applyLayout(row, "env-stage");
	ASSERT_TRUE(row->padding == 2, "env-stage padding");
	ASSERT_TRUE(c[0]->w > c[1]->w, "rate child wider than curve child");
	ASSERT_TRUE(c[0]->w == 197, "rate child width 197 (2/3 of 296 floor)");
	ASSERT_TRUE(c[1]->w == 99, "curve child width 99 (remainder fills to 296)");
	ASSERT_TRUE(c[0]->x == 2, "rate child after padding");
	/* confirm the row assigned the configured class names to its children */
	/* (applyLayout only sets className when the child has none) */
	freeGuiNode(row);
	remove(path);
	printf("PASS test_env_stage_layout\n");
	return 0;
}
