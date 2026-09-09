#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "gui_style.h"
#include "graph_gui.h"
#include "cJSON.h"
#include "theme.h"

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

static int test_compile_custom_dial_class(void);
static int test_compile_unknown_class_ignored(void);
static int test_missing_layout_file_keeps_defaults(void);
static int test_custom_class_geometry(void);
static int test_btn_and_typelabel_styles(void);
static int test_apply_layout_weights_and_classes(void);

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
	ASSERT_TRUE(g.valueX == 106 + 40, "geometry valueX with offset");
	ASSERT_TRUE(g.valueY == 204 + 6, "geometry valueY");
	ASSERT_TRUE(g.labelX == 104 - 40, "geometry labelX");
	ASSERT_TRUE(g.labelY == 204 + 24, "geometry labelY");
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
