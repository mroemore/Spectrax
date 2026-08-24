/*
 * nav_harness — visual testing harness for the graph navigation code.
 *
 * Generates a set of varied layout graphs, then for each layout walks
 * through (selection, key-press) steps:
 *
 *   1. select element, render, screenshot #1
 *   2. press a navigation key (navigateGraph), render, screenshot #2
 *   3. write prompt.txt + a manifest entry describing the step
 *
 * The manifest + screenshots are consumed by eval.py, which sends the
 * prompts to a vision model and compares the answer against the recorded
 * expected selection.
 *
 * Usage: ./nav_harness [--layouts N] [--keys l,r,u,d] [--out DIR]
 *
 * Compile with -DDEBUG_GRAPH_DRAW so drawNode renders the debug palette
 * (red = selectable, thick red fill = selected, yellow = container).
 *
 * Requires an X display (Xvfb works: xvfb-run -s "-screen 0 1280x800x24").
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* raylib and X11 both define a `Font` typedef — rename X11's away */
#define Font _X11_Font
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#undef Font

#include "graph_gui.h"
#include "input.h"

/* must match SCREEN_W/SCREEN_H in settings.h — initGuiNode rejects larger */
#define WIN_W 640
#define WIN_H 480
#define MAX_SELECTABLE 12
#define MAX_DEPTH 4

typedef struct {
	GuiNode *nodes[MAX_SELECTABLE];
	int count;
} Layout;

static uint32_t rng_state;

static uint32_t rng(void) {
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

static int irand(int lo, int hi) {
	return lo + (int)(rng() % (uint32_t)(hi - lo + 1));
}

static const char *key_name(int km) {
	switch(km) {
		case KM_LEFT:  return "left arrow";
		case KM_RIGHT: return "right arrow";
		case KM_UP:    return "up arrow";
		case KM_DOWN:  return "down arrow";
		default:       return "unknown";
	}
}

/* ---- layout generation ------------------------------------------------ */

static GuiNode *makeContainer(GuiNode *parent, int level, NodeAlignment na,
                              int padding, const char *name) {
	GuiNode *c = createGuiNode(10, 10, 100, 100, padding, na, name, false, false);
	appendItem(parent, c, irand(1, 4));
	return c;
}

static GuiNode *makeSelectable(Layout *lay, GuiNode *parent, NodeAlignment na,
                               char label, int level) {
	GuiNode *s = createGuiNode(10, 10, 60, 40, 6, na, NULL, true, false);
	free(s->name);
	s->name = malloc(2);
	s->name[0] = label;
	s->name[1] = '\0';
	appendItem(parent, s, irand(1, 4));
	if(lay->count < MAX_SELECTABLE) {
		lay->nodes[lay->count] = s;
		lay->count++;
	}
	return s;
}

static void genLevel(Layout *lay, GuiNode *parent, int level, int maxDepth) {
	int style;
	if(level >= maxDepth) {
		style = 3;
	} else {
		style = irand(0, 2);
	}

	/* track whether we placed any direct selectable siblings in this pass */
	int directCount = 0;
	switch(style) {
		case 0: /* horizontal band of direct selectables */
		{
			int n = irand(3, 6);
			for(int i = 0; i < n; i++) {
				if(lay->count >= MAX_SELECTABLE) return;
				makeSelectable(lay, parent, na_horizontal, (char)('A' + lay->count), level);
				directCount++;
			}
			break;
		}
		case 1: /* mix: direct selectables + subcontainers */
		{
			int n = irand(2, 4);
			for(int i = 0; i < n; i++) {
				if(lay->count >= MAX_SELECTABLE) return;
				if(level < maxDepth && irand(0, 1)) {
					GuiNode *sub = makeContainer(parent, level + 1,
					                             level % 2 == 0 ? na_horizontal : na_vertical,
					                             irand(4, 10), " ");
					genLevel(lay, sub, level + 1, maxDepth);
				} else {
					makeSelectable(lay, parent, level % 2 == 0 ? na_horizontal : na_vertical,
					               (char)('A' + lay->count), level);
					directCount++;
				}
			}
			break;
		}
		case 2: /* pure subcontainers */
		{
			int n = irand(2, 3);
			for(int i = 0; i < n; i++) {
				if(lay->count >= MAX_SELECTABLE) return;
				GuiNode *sub = makeContainer(parent, level + 1,
				                             level % 2 == 0 ? na_horizontal : na_vertical,
				                             irand(4, 10), " ");
				genLevel(lay, sub, level + 1, maxDepth);
			}
			break;
		}
		case 3: /* leaf container: direct selectables only */
		{
			int n = irand(2, 5);
			for(int i = 0; i < n; i++) {
				if(lay->count >= MAX_SELECTABLE) return;
				makeSelectable(lay, parent, level % 2 == 0 ? na_horizontal : na_vertical,
				               (char)('A' + lay->count), level);
				directCount++;
			}
			break;
		}
	}

	(void)directCount;
}

/*
 * Build one layout with `seed`. Guarantees (retried until satisfied):
 *  - at least 9 selectable elements
 *  - at least one container with >= 3 direct selectable children
 *  - at least one nested container chain of depth >= 2 below a direct
 *    child (distant leaves)
 */
static void dumpGeometry(GuiNode *n, int depth) {
	for(int i = 0; i < depth; i++) {
		printf("  ");
	}
	printf("'%s' x=%d y=%d w=%d h=%d pad=%d align=%d sel=%d itc=%d wts=%d\n",
	       n->name, n->x, n->y, n->w, n->h, n->padding, n->nodeAlignment,
	       n->selected, n->itemCount, n->totalItemWeights);
	if(n->itemCount > 0) {
		ListElement *cur = n->items->head;
		for(int i = 0; i < n->itemCount; i++) {
			GuiNode *gn = *(GuiNode **)cur->data;
			dumpGeometry(gn, depth + 1);
			cur = cur->next;
		}
	}
}

static int buildLayout(Layout *lay, int seed, int maxDepth) {	rng_state = (uint32_t)(1000 + seed * 7919);
	memset(lay, 0, sizeof(*lay));

	GuiNode *root = createGuiNode(0, 0, WIN_W, WIN_H, 8, na_vertical, "root", false, false);
	genLevel(lay, root, 0, maxDepth);
	reflowCoordinates(root);

	/* verify the structural guarantees */
	int hasSiblingGroup = 0;
	int hasDeepChain = 0;
	int tinyNodes = 0;
	for(int i = 0; i < lay->count; i++) {
		GuiNode *n = lay->nodes[i];
		GuiNode *c = n->container;
		if(!c) continue;
		/* every selectable needs at least 4px of internal space on both
		 * axes, otherwise it renders as a blob (or worse, a uint16 wrap) */
		if(n->h < n->padding * 2 + 4 || n->w < n->padding * 2 + 4) {
			tinyNodes = 1;
		}
		/* count direct selectable siblings of n */
		int sib = 0;
		if(c->itemCount > 0) {
			ListElement *cur = c->items->head;
			for(int j = 0; j < c->itemCount; j++) {
				GuiNode *gn = *(GuiNode **)cur->data;
				if(gn->selectable) sib++;
				cur = cur->next;
			}
		}
		if(sib >= 3) hasSiblingGroup = 1;
		/* chain: container of a container holding selectables */
		if(c->container && c->itemCount > 0) {
			ListElement *cur = c->items->head;
			for(int j = 0; j < c->itemCount; j++) {
				GuiNode *gn = *(GuiNode **)cur->data;
				if(gn->selectable) {
					hasDeepChain = 1;
					break;
				}
			}
		}
	}

	if(lay->count < 9 || !hasSiblingGroup || !hasDeepChain || tinyNodes) {
		return 0;
	}
	return 1;
}

/*
 * raylib TakeScreenshot/LoadImageFromScreen read back a blank frame under
 * Xvfb/llvmpipe, so capture the presented window contents via X11 instead
 * (XGetImage). GetWindowHandle() returns the GLFW struct, not the X Window
 * id — locate the window by walking the root's children.
 */
static Window findWin(Display *dpy, int w, int h) {
	Window root = DefaultRootWindow(dpy);
	Window parent;
	Window *kids = NULL;
	unsigned int n = 0;
	Window found = 0;
	if(!XQueryTree(dpy, root, &root, &parent, &kids, &n)) return 0;
	for(unsigned int i = 0; i < n; i++) {
		XWindowAttributes a;
		if(XGetWindowAttributes(dpy, kids[i], &a) &&
		   a.map_state == IsViewable && a.width == w && a.height == h) {
			found = kids[i];
			break;
		}
	}
	if(kids) XFree(kids);
	return found;
}

static int shotX11(const char *path) {
	Display *dpy = XOpenDisplay(NULL);
	if(!dpy) return 1;
	Window win = findWin(dpy, WIN_W, WIN_H);
	if(!win) {
		XCloseDisplay(dpy);
		return 1;
	}
	XImage *img = XGetImage(dpy, win, 0, 0, WIN_W, WIN_H, AllPlanes, ZPixmap);
	if(!img) {
		XCloseDisplay(dpy);
		return 1;
	}
	int bpp = img->bits_per_pixel / 8;
	unsigned char *pix = malloc((size_t)WIN_W * WIN_H * 4);
	unsigned char *src = (unsigned char *)img->data;
	for(int i = 0; i < WIN_W * WIN_H; i++) {
		unsigned char *s = src + (size_t)i * bpp;
		pix[i * 4 + 0] = s[2];
		pix[i * 4 + 1] = s[1];
		pix[i * 4 + 2] = s[0];
		pix[i * 4 + 3] = 255;
	}
	XDestroyImage(img);
	XCloseDisplay(dpy);
	Image ri = { 0 };
	ri.width = WIN_W;
	ri.height = WIN_H;
	ri.mipmaps = 1;
	ri.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
	ri.data = pix;
	bool ok = ExportImage(ri, path);
	free(pix);
	return ok ? 0 : 1;
}

static void mkdirs(const char *path) {
	char buf[512];
	snprintf(buf, sizeof(buf), "%s", path);
	for(char *p = buf + 1; *p; p++) {
		if(*p == '/') {
			*p = '\0';
			mkdir(buf, 0755);
			*p = '/';
		}
	}
	mkdir(buf, 0755);
}

static void writePrompt(const char *path, const char *labels, char selBefore,
                        const char *key) {
	FILE *f = fopen(path, "w");
	if(!f) return;
	fprintf(f,
	        "This is a prototype for a UI we are working on. Navigation between "
	        "selected UI elements is done using the arrow keys only. Every "
	        "labelled element from %s is selectable. The currently selected item "
	        "is highlighted in red. Currently the item selected is %c. Which item "
	        "should be highlighted if you pressed the %s button?\n",
	        labels, selBefore, key);
	fclose(f);
}

/* ---- main ------------------------------------------------------------- */

int main(int argc, char **argv) {
	int layoutCount = 6;
	char keyset[4] = { KM_LEFT, KM_RIGHT, KM_UP, KM_DOWN };
	int keyCount = 4;
	const char *outDir = ".tmp_files/nav_harness_out";

	for(int i = 1; i < argc; i++) {
		if(strcmp(argv[i], "--layouts") == 0 && i + 1 < argc) {
			layoutCount = atoi(argv[++i]);
		} else if(strcmp(argv[i], "--keys") == 0 && i + 1 < argc) {
			keyCount = 0;
			char *s = argv[++i];
			for(char *p = s; *p && keyCount < 4; p++) {
				switch(*p) {
					case 'l': keyset[keyCount++] = KM_LEFT; break;
					case 'r': keyset[keyCount++] = KM_RIGHT; break;
					case 'u': keyset[keyCount++] = KM_UP; break;
					case 'd': keyset[keyCount++] = KM_DOWN; break;
				}
			}
		} else if(strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
			outDir = argv[++i];
		}
	}

	mkdirs(outDir);
	char manifestPath[512];
	snprintf(manifestPath, sizeof(manifestPath), "%s/manifest.jsonl", outDir);
	FILE *manifest = fopen(manifestPath, "w");
	if(!manifest) {
		fprintf(stderr, "cannot open manifest: %s\n", manifestPath);
		return 1;
	}

	InitWindow(WIN_W, WIN_H, "nav_harness");
	SetTargetFPS(60);

	/* raylib TakeScreenshot ignores the path component and always writes
	 * to cwd/<basename> — screenshot under a unique basename, then move. */

	Layout lay;
	int stepIndex = 0;
	for(int li = 0; li < layoutCount; li++) {
		GuiNode *root = NULL;
		int maxDepth = 2 + (li % (MAX_DEPTH - 1));
		int ok = 0;
		for(int attempt = 0; attempt < 8 && !ok; attempt++) {
			ok = buildLayout(&lay, li * 7 + attempt, maxDepth);
		}
		if(!ok) {
			fprintf(stderr, "layout %d: could not satisfy structural guarantees\n", li);
			continue;
		}

		root = lay.nodes[0]->container;
		/* walk up to the real root */
		while(root->container) {
			root = root->container;
		}

		reflowCoordinates(root);

		if(getenv("NAV_DUMP")) {
			dumpGeometry(root, 0);
		}

		/* label range string, e.g. "A to I" */
		char labels[32];
		if(lay.count > 2) {
			snprintf(labels, sizeof(labels), "%c to %c",
			         'A', (char)('A' + lay.count - 1));
		} else {
			snprintf(labels, sizeof(labels), "%c", 'A');
		}

		Graph *g = (Graph *)malloc(sizeof(Graph));
		g->root = root;
		g->selected = lay.nodes[0];
		g->selected->selected = 1;

		for(int si = 0; si < lay.count; si++) {
			for(int ki = 0; ki < keyCount; ki++, stepIndex++) {
				int km = keyset[ki];
				char stepDir[512];
				snprintf(stepDir, sizeof(stepDir), "%s/layout_%03d/step_%03d",
				         outDir, li, stepIndex);
				mkdirs(stepDir);

				char s1[600], s2[600], promptPath[600];
				snprintf(s1, sizeof(s1), "%s/1.png", stepDir);
				snprintf(s2, sizeof(s2), "%s/2.png", stepDir);

				changeGraphSelection(g, lay.nodes[si]);
				BeginDrawing();
				ClearBackground(BLACK);
				drawNode(g->root);
				EndDrawing();
				shotX11(s1);

				navigateGraph(g, km);

				BeginDrawing();
				ClearBackground(BLACK);
				drawNode(g->root);
				EndDrawing();
				shotX11(s2);

				snprintf(promptPath, sizeof(promptPath), "%s/prompt.txt", stepDir);
				char selBefore = lay.nodes[si]->name[0];
				writePrompt(promptPath, labels, selBefore, key_name(km));

				char expected = g->selected ? g->selected->name[0] : '?';
				fprintf(manifest,
				        "{\"layout\":%d,\"step\":%d,\"labels\":\"%s\","
				        "\"selected_before\":\"%c\",\"key\":\"%s\","
				        "\"expected\":\"%c\","
				        "\"s1\":\"layout_%03d/step_%03d/1.png\","
				        "\"s2\":\"layout_%03d/step_%03d/2.png\"}\n",
				        li, stepIndex, labels, selBefore, key_name(km), expected,
				        li, stepIndex, li, stepIndex);
			}
		}
		free(g);
	}

	fclose(manifest);
	CloseWindow();
	printf("done: %d steps -> %s/\n", stepIndex, outDir);
	return 0;
}
