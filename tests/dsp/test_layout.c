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

static int test_compile_applies_dial_overrides(void);
static int test_extends_chain_merges_through_aliases(void);
static int test_extends_chain_self_reference_falls_back(void);

int main(void) {
	int fails = 0;
	ensure_tmp_dirs();
	fails += test_baked_defaults_used_when_no_class();
	fails += test_unknown_class_falls_back_to_type_default();
	fails += test_compute_dial_geometry();
	fails += test_compile_applies_dial_overrides();
	fails += test_extends_chain_merges_through_aliases();
	fails += test_extends_chain_self_reference_falls_back();
	if(fails) {
		printf("%d layout test(s) failed\n", fails);
		return 1;
	}
	printf("ALL layout tests passed\n");
	return 0;
}

static int test_compile_applies_dial_overrides(void) {
	const char *path = TMP_DIR "layout_compile_dial.json";
	FILE *f = fopen(path, "w");
	ASSERT_TRUE(f != NULL, "open layout.json for write");
	const char *src =
		"{\n"
		"  \"$extends\": \"dial\",\n"
		"  \"class:big\": {\n"
		"    \"type\": \"dial\",\n"
		"    \"knob\": { \"size\": 36 },\n"
		"    \"value\": { \"format\": \"%05.3f\" }\n"
		"  }\n"
		"}\n";
	fputs(src, f);
	fclose(f);

	ColourScheme cs = { 0 };
	cs.fontColour = (Color){ 200, 200, 200, 255 };
	cs.outlineColour = (Color){ 255, 255, 255, 255 };
	ASSERT_TRUE(compileLayoutConfig(path, &cs), "compileLayoutConfig returned true");

	GuiNode *n = createBlankGuiNode();
	guiNodeSetClass(n, "big");
	const DialStyle *d = resolveDialStyle(n);
	ASSERT_TRUE(d->knob.size == 36, "override knob.size == 36");
	ASSERT_TRUE(strcmp(d->value.format, "%05.3f") == 0, "override value.format");
	ASSERT_TRUE(d->knob.startAngle == -225, "fallback startAngle from baked");
	freeGuiNode(n);
	printf("PASS test_compile_applies_dial_overrides\n");
	return 0;
}

static int test_extends_chain_merges_through_aliases(void) {
	const char *path = TMP_DIR "layout_compile_chain.json";
	FILE *f = fopen(path, "w");
	ASSERT_TRUE(f != NULL, "open layout.json for write");
	const char *src =
		"{\n"
		"  \"$extends\": \"dial\",\n"
		"  \"class:alias-a\": {\n"
		"    \"type\": \"dial\",\n"
		"    \"$extends\": \"alias-b\",\n"
		"    \"knob\": { \"size\": 30 }\n"
		"  },\n"
		"  \"class:alias-b\": {\n"
		"    \"type\": \"dial\",\n"
		"    \"$extends\": \"dial\",\n"
		"    \"value\": { \"offsetX\": 50 }\n"
		"  }\n"
		"}\n";
	fputs(src, f);
	fclose(f);

	ColourScheme cs = { 0 };
	cs.fontColour = (Color){ 200, 200, 200, 255 };
	cs.outlineColour = (Color){ 255, 255, 255, 255 };
	ASSERT_TRUE(compileLayoutConfig(path, &cs), "compileLayoutConfig returned true");

	GuiNode *n = createBlankGuiNode();
	guiNodeSetClass(n, "alias-a");
	const DialStyle *d = resolveDialStyle(n);
	ASSERT_TRUE(d->knob.size == 30, "alias-a wins knob.size");
	ASSERT_TRUE(d->value.offsetX == 50, "alias-b override propagated");
	freeGuiNode(n);
	printf("PASS test_extends_chain_merges_through_aliases\n");
	return 0;
}

static int test_extends_chain_self_reference_falls_back(void) {
	const char *path = TMP_DIR "layout_compile_self.json";
	FILE *f = fopen(path, "w");
	ASSERT_TRUE(f != NULL, "open layout.json for write");
	const char *src =
		"{\n"
		"  \"$extends\": \"dial\",\n"
		"  \"class:loopy\": {\n"
		"    \"type\": \"dial\",\n"
		"    \"$extends\": \"loopy\"\n"
		"  }\n"
		"}\n";
	fputs(src, f);
	fclose(f);

	ColourScheme cs = { 0 };
	cs.fontColour = (Color){ 200, 200, 200, 255 };
	cs.outlineColour = (Color){ 255, 255, 255, 255 };
	ASSERT_TRUE(compileLayoutConfig(path, &cs), "compileLayoutConfig returned true");

	GuiNode *n = createBlankGuiNode();
	guiNodeSetClass(n, "loopy");
	const DialStyle *d = resolveDialStyle(n);
	ASSERT_TRUE(d->knob.size == 20, "loopy falls back to baked default knob.size");
	freeGuiNode(n);
	printf("PASS test_extends_chain_self_reference_falls_back\n");
	return 0;
}
