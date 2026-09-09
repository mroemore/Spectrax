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
