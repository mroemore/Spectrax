/*
 * theme_tool -- the companion tool for
 * manual/tutorials/shipping-a-theme.md.
 *
 * It answers the three questions the theme-authoring workflow keeps
 * asking, without starting the GUI:
 *
 *   where   which config dir does the app resolve, and what will it read?
 *   check   is this theme file complete, canonical and free of typos?
 *   canon   what will the app write to this theme file when it exits?
 *
 * The tool links the app's own config code -- src/paths.c for the
 * directory resolution, src/io/config_io.c for the theme load/save and
 * the canonical path table, src/gui_core.c for the default colour
 * scheme -- so its answers cannot drift from what the app does.
 *
 * It is a development tool: nothing here is linked into `spectrax`, and
 * meson/install.sh does not copy it into bin/.
 *
 * Usage:
 *   theme_tool where                   resolve the config/data dirs
 *   theme_tool check <theme.json>      audit a theme file
 *   theme_tool canon <theme.json>      print the canonical grouped form
 *   theme_tool canon <theme.json> -o F write the canonical form to F
 *   theme_tool tokens                  list the canonical colour paths
 *   theme_tool --selftest              headless self-check
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "gui.h"
#include "io/config_io.h"
#include "paths.h"
#include "theme.h"

#define MAXPATH 512
#define MAX_NAMES 64

static int g_failures = 0;

#define CHECK(cond, msg) do { \
	if(!(cond)) { printf("FAIL %s\n", msg); g_failures++; } \
} while(0)

/* --- small file helpers ------------------------------------------------- */

static bool fileExists(const char *path) {
	struct stat st;
	return stat(path, &st) == 0;
}

static bool dirExists(const char *path) {
	struct stat st;
	if(stat(path, &st) != 0) {
		return false;
	}
	return S_ISDIR(st.st_mode);
}

static char *slurp(const char *path, size_t *lenOut) {
	FILE *f = fopen(path, "rb");
	if(!f) {
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if(sz <= 0 || sz > (1 << 20)) {
		fclose(f);
		return NULL;
	}
	char *buf = malloc((size_t)sz + 1);
	if(!buf) {
		fclose(f);
		return NULL;
	}
	size_t got = fread(buf, 1, (size_t)sz, f);
	fclose(f);
	buf[got] = '\0';
	if(lenOut) {
		*lenOut = got;
	}
	return buf;
}

static bool writeText(const char *path, const char *text) {
	FILE *f = fopen(path, "wb");
	if(!f) {
		return false;
	}
	bool ok = fputs(text, f) >= 0;
	fclose(f);
	return ok;
}

/* --- the canonical path table ------------------------------------------- */

/* Index of a canonical path ("text.primary") in the theme path table, or
 * -1. The table is the app's single source of truth for the mapping
 * between JSON names and ColourScheme fields. */
static int canonicalIndex(const char *path) {
	for(int i = 0; i < themePathCount(); i++) {
		const char *n = themePathName(i);
		if(n && strcmp(n, path) == 0) {
			return i;
		}
	}
	return -1;
}

/* The canonical path that maps to the same ColourScheme field as `name`.
 * `name` may be a legacy flat alias ("chipPalette3") -- themeFieldByName
 * resolves those through the alias table, and this maps the result back
 * to the canonical path. Returns NULL when the name resolves to nothing. */
static const char *canonicalPathFor(ColourScheme *cs, const char *name) {
	Color *f = themeFieldByName(cs, name);
	if(!f) {
		return NULL;
	}
	for(int i = 0; i < themePathCount(); i++) {
		if(themeFieldByName(cs, themePathName(i)) == f) {
			return themePathName(i);
		}
	}
	return NULL;
}

/* --- theme audit -------------------------------------------------------- */

typedef struct {
	bool provided[64];             /* per canonical path index */
	int providedCount;
	int missingCount;
	int unknownCount;
	int legacyCount;
	int zeroAlphaCount;
	int badValueCount;
	char missingNames[MAX_NAMES][96];
	char unknownNames[MAX_NAMES][96];
	char legacyNames[MAX_NAMES][96];
	char zeroAlphaNames[MAX_NAMES][96];
} Audit;

static void auditInit(Audit *a) {
	memset(a, 0, sizeof(*a));
}

/* Append a name to a bounded list and bump its counter. The counter is
 * the list length, so it is also the "how many" number the report prints. */
static void auditNote(char (*list)[96], int *n, const char *name) {
	if(*n < MAX_NAMES) {
		snprintf(list[*n], 96, "%s", name);
	}
	(*n)++;
}

/* A colour value: must be a string, must parse as #RRGGBB[AA], and must
 * not be fully transparent (the loader treats alpha 0 as "unset"). */
static void auditValue(Audit *a, const char *path, const char *value, bool isString) {
	if(!isString) {
		a->badValueCount++;
		return;
	}
	Color c;
	if(!parseHexColor(value, &c)) {
		a->badValueCount++;
		return;
	}
	if(c.a == 0) {
		auditNote(a->zeroAlphaNames, &a->zeroAlphaCount, path);
	}
}

/* Classify one leaf: a "group.leaf" path with a colour value. */
static void auditLeaf(Audit *a, ColourScheme *cs, const char *path, const char *value,
                      bool isString) {
	int idx = canonicalIndex(path);
	if(idx >= 0) {
		if(!a->provided[idx]) {
			a->provided[idx] = true;
			a->providedCount++;
		}
	} else {
		const char *canon = canonicalPathFor(cs, path);
		if(canon) {
			int ci = canonicalIndex(canon);
			if(ci >= 0 && !a->provided[ci]) {
				a->provided[ci] = true;
				a->providedCount++;
			}
			char line[96];
			snprintf(line, sizeof(line), "%s -> %s", path, canon);
			auditNote(a->legacyNames, &a->legacyCount, line);
		} else {
			auditNote(a->unknownNames, &a->unknownCount, path);
		}
	}
	auditValue(a, path, value, isString);
}

/* Walk the `colors` object of a theme document. */
static void auditColors(Audit *a, ColourScheme *cs, cJSON *obj, const char *prefix) {
	cJSON *item = NULL;
	cJSON_ArrayForEach(item, obj) {
		char path[160];
		if(prefix[0]) {
			snprintf(path, sizeof(path), "%s.%s", prefix, item->string ? item->string : "?");
		} else {
			snprintf(path, sizeof(path), "%s", item->string ? item->string : "?");
		}
		if(cJSON_IsObject(item)) {
			auditColors(a, cs, item, path);
			continue;
		}
		if(cJSON_IsArray(item)) {
			/* The only array the loader understands is chip.palette,
			 * which is chip.palette0 .. chip.palette7. */
			if(strcmp(path, "chip.palette") == 0) {
				int n = cJSON_GetArraySize(item);
				for(int i = 0; i < n; i++) {
					cJSON *el = cJSON_GetArrayItem(item, i);
					char leaf[64];
					snprintf(leaf, sizeof(leaf), "chip.palette%d", i);
					int ci = canonicalIndex(leaf);
					if(ci >= 0 && !a->provided[ci]) {
						a->provided[ci] = true;
						a->providedCount++;
					}
					if(cJSON_IsString(el)) {
						auditValue(a, leaf, el->valuestring, true);
					} else {
						a->badValueCount++;
					}
				}
				continue;
			}
			auditNote(a->unknownNames, &a->unknownCount, path);
			continue;
		}
		auditLeaf(a, cs, path, cJSON_IsString(item) ? item->valuestring : NULL,
		          cJSON_IsString(item));
	}
}

static int auditThemeFile(const char *path, Audit *a) {
	auditInit(a);
	char *buf = slurp(path, NULL);
	if(!buf) {
		printf("cannot read %s\n", path);
		return 0;
	}
	cJSON *doc = cJSON_Parse(buf);
	free(buf);
	if(!doc) {
		printf("%s is not valid JSON\n", path);
		return 0;
	}
	ColourScheme cs;
	memset(&cs, 0, sizeof(cs));
	cJSON *colors = cJSON_GetObjectItemCaseSensitive(doc, "colors");
	if(cJSON_IsObject(colors)) {
		auditColors(a, &cs, colors, "");
	}
	/* Everything not provided is missing. */
	for(int i = 0; i < themePathCount() && i < 64; i++) {
		if(!a->provided[i]) {
			auditNote(a->missingNames, &a->missingCount, themePathName(i));
		}
	}
	cJSON_Delete(doc);
	return 1;
}

/* --- bound --------------------------------------------------------------- */

/* A class only reaches a widget if something asks for it by name. In
 * the config, the only way to do that is a layout's `childClasses`
 * array; in C it is guiNodeSetClass(). This command reports the
 * config-side view: which classes a layout binds, which classes are
 * only reachable as an `extends` ancestor of a bound class, and which
 * classes nothing asks for at all. */

#define MAX_CLASSES 96

typedef struct {
	char name[64];
	char parent[64];   /* the `extends` target, "" when it is a type key */
	bool bound;        /* named by a layout's childClasses */
	bool ancestor;     /* an ancestor of a bound class */
} ClassEntry;

/* Collect the class table from a parsed style config: every entry under
 * "styles", with the `extends` target it inherits from. */
static int collectClasses(cJSON *doc, ClassEntry *classes, int maxClasses) {
	int classCount = 0;
	cJSON *styles = cJSON_GetObjectItemCaseSensitive(doc, "styles");
	if(!cJSON_IsObject(styles)) {
		return 0;
	}
	cJSON *it = NULL;
	cJSON_ArrayForEach(it, styles) {
		if(classCount >= maxClasses) {
			break;
		}
		ClassEntry *e = &classes[classCount++];
		memset(e, 0, sizeof(*e));
		snprintf(e->name, sizeof(e->name), "%s", it->string ? it->string : "?");
		cJSON *ext = cJSON_GetObjectItemCaseSensitive(it, "extends");
		if(cJSON_IsString(ext)) {
			snprintf(e->parent, sizeof(e->parent), "%s", ext->valuestring);
		}
	}
	return classCount;
}

/* Mark the classes a layout's childClasses arrays name. */
static void markBound(cJSON *doc, ClassEntry *classes, int classCount) {
	cJSON *layouts = cJSON_GetObjectItemCaseSensitive(doc, "layouts");
	if(!cJSON_IsObject(layouts)) {
		return;
	}
	cJSON *l = NULL;
	cJSON_ArrayForEach(l, layouts) {
		cJSON *cc = cJSON_GetObjectItemCaseSensitive(l, "childClasses");
		if(!cJSON_IsArray(cc)) {
			continue;
		}
		int n = cJSON_GetArraySize(cc);
		for(int i = 0; i < n; i++) {
			cJSON *e = cJSON_GetArrayItem(cc, i);
			if(!cJSON_IsString(e) || !e->valuestring[0]) {
				continue;
			}
			for(int c = 0; c < classCount; c++) {
				if(strcmp(classes[c].name, e->valuestring) == 0) {
					classes[c].bound = true;
				}
			}
		}
	}
}

/* An ancestor of a bound class still contributes: the chain is
 * overlaid root -> leaf. */
static void markAncestors(ClassEntry *classes, int classCount) {
	bool changed = true;
	while(changed) {
		changed = false;
		for(int i = 0; i < classCount; i++) {
			if(!(classes[i].bound || classes[i].ancestor) || !classes[i].parent[0]) {
				continue;
			}
			for(int j = 0; j < classCount; j++) {
				if(strcmp(classes[j].name, classes[i].parent) == 0 && !classes[j].ancestor) {
					classes[j].ancestor = true;
					changed = true;
				}
			}
		}
	}
}

static int runBound(const char *path) {
	char *buf = slurp(path, NULL);
	if(!buf) {
		printf("cannot read %s\n", path);
		return 1;
	}
	cJSON *doc = cJSON_Parse(buf);
	free(buf);
	if(!doc) {
		printf("%s is not valid JSON\n", path);
		return 1;
	}
	ClassEntry classes[MAX_CLASSES];
	int classCount = collectClasses(doc, classes, MAX_CLASSES);
	markBound(doc, classes, classCount);
	markAncestors(classes, classCount);

	printf("layout: %s\n", path);
	cJSON *layouts = cJSON_GetObjectItemCaseSensitive(doc, "layouts");
	if(cJSON_IsObject(layouts)) {
		cJSON *l = NULL;
		cJSON_ArrayForEach(l, layouts) {
			cJSON *cc = cJSON_GetObjectItemCaseSensitive(l, "childClasses");
			if(!cJSON_IsArray(cc)) {
				continue;
			}
			printf("  layout %-24s binds:", l->string ? l->string : "?");
			int n = cJSON_GetArraySize(cc);
			int printed = 0;
			for(int i = 0; i < n; i++) {
				cJSON *e = cJSON_GetArrayItem(cc, i);
				if(!cJSON_IsString(e) || !e->valuestring[0]) {
					continue;
				}
				printf("%s %s", printed ? "," : "", e->valuestring);
				printed++;
			}
			if(!printed) {
				printf(" (nothing)");
			}
			printf("\n");
		}
	}
	printf("  %d classes defined\n", classCount);
	printf("  bound by a layout's childClasses:");
	for(int i = 0; i < classCount; i++) {
		if(classes[i].bound) {
			printf(" %s", classes[i].name);
		}
	}
	printf("\n");
	printf("  used only as an extends ancestor:");
	{
		int printed = 0;
		for(int i = 0; i < classCount; i++) {
			if(classes[i].ancestor && !classes[i].bound) {
				printf("%s %s", printed ? "," : "", classes[i].name);
				printed++;
			}
		}
		if(!printed) {
			printf(" (none)");
		}
		printf("\n");
	}
	printf("  not bound by anything:");
	{
		int printed = 0;
		for(int i = 0; i < classCount; i++) {
			if(!classes[i].bound && !classes[i].ancestor) {
				printf("%s %s", printed ? "," : "", classes[i].name);
				printed++;
			}
		}
		if(!printed) {
			printf(" (none)");
		}
		printf("\n");
	}
	printf("  note: a class can also be bound in C with guiNodeSetClass();\n"
	       "        the config cannot see that.\n");
	cJSON_Delete(doc);
	return 0;
}

/* --- where -------------------------------------------------------------- */

static const char *argValue(int argc, char **argv, const char *flag) {
	for(int i = 1; i < argc - 1; i++) {
		if(strcmp(argv[i], flag) == 0) {
			return argv[i + 1];
		}
	}
	return NULL;
}

static void printCfgCandidate(const char *label, const char *dir, bool isWinner) {
	char cfg[MAXPATH];
	snprintf(cfg, sizeof(cfg), "%s/cfg.json", dir);
	printf("  %-34s %-44s cfg.json: %-3s%s\n", label, dir,
	       fileExists(cfg) ? "yes" : "no", isWinner ? "   <- wins" : "");
}

static void printDataCandidate(const char *label, const char *dir, bool isWinner) {
	printf("  %-34s %-44s dir: %-5s%s\n", label, dir,
	       dirExists(dir) ? "yes" : "no", isWinner ? "   <- wins" : "");
}

static int runWhere(int argc, char **argv) {
	char cfgDir[MAXPATH];
	char dataDir[MAXPATH];
	resolveConfigDir(argc, argv, cfgDir, sizeof(cfgDir));
	resolveDataDir(argc, argv, dataDir, sizeof(dataDir));

	char cwd[MAXPATH];
	if(!getcwd(cwd, sizeof(cwd))) {
		snprintf(cwd, sizeof(cwd), ".");
	}

	printf("config dir candidates (first hit wins, src/paths.c):\n");
	const char *flagCfg = argValue(argc, argv, "--config-dir");
	if(flagCfg) {
		printCfgCandidate("1. --config-dir", flagCfg, strcmp(flagCfg, cfgDir) == 0);
	} else {
		printf("  %-34s %-44s %s\n", "1. --config-dir", "(not given)", "");
	}
	const char *xdg = getenv("XDG_CONFIG_HOME");
	if(xdg && *xdg) {
		char p[MAXPATH];
		snprintf(p, sizeof(p), "%s/spectrax", xdg);
		printCfgCandidate("2. $XDG_CONFIG_HOME/spectrax", p, strcmp(p, cfgDir) == 0);
	} else {
		printf("  %-34s %-44s %s\n", "2. $XDG_CONFIG_HOME/spectrax", "(unset)", "");
	}
	const char *home = getenv("HOME");
	if(home && *home) {
		char p[MAXPATH];
		snprintf(p, sizeof(p), "%s/.config/spectrax", home);
		printCfgCandidate("3. $HOME/.config/spectrax", p, strcmp(p, cfgDir) == 0);
	} else {
		printf("  %-34s %-44s %s\n", "3. $HOME/.config/spectrax", "(unset)", "");
	}
	printCfgCandidate("4. . (cwd)", cwd, strcmp(cwd, cfgDir) == 0);
	printf("resolved config dir: %s\n\n", cfgDir);

	printf("data dir candidates (first hit wins, src/paths.c):\n");
	const char *flagData = argValue(argc, argv, "--data-dir");
	if(flagData) {
		printDataCandidate("1. --data-dir", flagData, strcmp(flagData, dataDir) == 0);
	} else {
		printf("  %-34s %-44s %s\n", "1. --data-dir", "(not given)", "");
	}
	const char *xdgData = getenv("XDG_DATA_HOME");
	if(xdgData && *xdgData) {
		char p[MAXPATH];
		snprintf(p, sizeof(p), "%s/spectrax", xdgData);
		printDataCandidate("2. $XDG_DATA_HOME/spectrax", p, strcmp(p, dataDir) == 0);
	} else {
		printf("  %-34s %-44s %s\n", "2. $XDG_DATA_HOME/spectrax", "(unset)", "");
	}
	if(home && *home) {
		char p[MAXPATH];
		snprintf(p, sizeof(p), "%s/.local/share/spectrax", home);
		printDataCandidate("3. $HOME/.local/share/spectrax", p, strcmp(p, dataDir) == 0);
	} else {
		printf("  %-34s %-44s %s\n", "3. $HOME/.local/share/spectrax", "(unset)", "");
	}
	printDataCandidate("4. . (cwd)", cwd, strcmp(cwd, dataDir) == 0);
	printf("resolved data dir: %s\n\n", dataDir);

	/* The theme file name comes from cfg.json's "theme" key. */
	char themeName[256];
	snprintf(themeName, sizeof(themeName), "clr.json");
	char cfgPath[2 * MAXPATH];
	snprintf(cfgPath, sizeof(cfgPath), "%s/cfg.json", cfgDir);
	Settings settings;
	memset(&settings, 0, sizeof(settings));
	loadSettingsJson(cfgPath, &settings, themeName, sizeof(themeName));
	if(themeName[0] == '\0') {
		snprintf(themeName, sizeof(themeName), "clr.json");
	}

	printf("files the app reads from the config dir:\n");
	char p[2 * MAXPATH];
	snprintf(p, sizeof(p), "%s/cfg.json", cfgDir);
	printf("  %-44s %s  (settings; names the theme file)\n", p,
	       fileExists(p) ? "present" : "MISSING");
	snprintf(p, sizeof(p), "%s/%s", cfgDir, themeName);
	printf("  %-44s %s  (theme: cfg.json names \"%s\")\n", p,
	       fileExists(p) ? "present" : "MISSING", themeName);
	snprintf(p, sizeof(p), "%s/layout.json", cfgDir);
	printf("  %-44s %s  (styles + layouts)\n", p,
	       fileExists(p) ? "present" : "MISSING");
	printf("\n");
	return 0;
}

/* --- check -------------------------------------------------------------- */

static int runCheck(const char *path) {
	Audit a;
	if(!auditThemeFile(path, &a)) {
		return 1;
	}
	printf("theme: %s\n", path);
	size_t len = 0;
	char *buf = slurp(path, &len);
	if(buf) {
		cJSON *doc = cJSON_Parse(buf);
		free(buf);
		if(doc) {
			cJSON *schema = cJSON_GetObjectItemCaseSensitive(doc, "schema");
			printf("  schema: %s\n",
			       cJSON_IsNumber(schema) ? "declared" : "not declared");
			cJSON *font = cJSON_GetObjectItemCaseSensitive(doc, "font");
			if(cJSON_IsObject(font)) {
				cJSON *fp = cJSON_GetObjectItemCaseSensitive(font, "path");
				cJSON *fs = cJSON_GetObjectItemCaseSensitive(font, "size");
				cJSON *fsp = cJSON_GetObjectItemCaseSensitive(font, "spacing");
				printf("  font: %s size=%d spacing=%d\n",
				       cJSON_IsString(fp) ? fp->valuestring : "(none)",
				       cJSON_IsNumber(fs) ? fs->valueint : 0,
				       cJSON_IsNumber(fsp) ? fsp->valueint : 0);
			} else {
				printf("  font: (none -- the app keeps its built-in defaults)\n");
			}
			cJSON_Delete(doc);
		}
	}
	printf("  colours: %d canonical keys, %d provided, %d missing, %d unknown, "
	       "%d legacy alias\n",
	       themePathCount(), a.providedCount, a.missingCount, a.unknownCount, a.legacyCount);
	for(int i = 0; i < a.missingCount; i++) {
		printf("    missing:  %s\n", a.missingNames[i]);
	}
	for(int i = 0; i < a.unknownCount; i++) {
		printf("    unknown:  %s\n", a.unknownNames[i]);
	}
	for(int i = 0; i < a.legacyCount; i++) {
		printf("    legacy:   %s\n", a.legacyNames[i]);
	}
	for(int i = 0; i < a.zeroAlphaCount; i++) {
		printf("    zero alpha: %s  (the loader treats this as unset)\n",
		       a.zeroAlphaNames[i]);
	}
	if(a.badValueCount) {
		printf("    %d value(s) are not #RRGGBB[AA] strings and are ignored\n",
		       a.badValueCount);
	}
	bool ok = a.missingCount == 0 && a.unknownCount == 0 && a.zeroAlphaCount == 0 &&
	          a.badValueCount == 0;
	printf("  %s\n", ok ? "OK: the theme covers every canonical colour path"
	                   : "FAIL");
	return ok ? 0 : 1;
}

/* --- canon -------------------------------------------------------------- */

/* Load the theme exactly the way main.c does -- seed the defaults, then
 * overlay the file -- and write the result back exactly the way the app
 * does on exit (saveThemeJson, canonical grouped schema 2). */
static int runCanon(const char *path, const char *outPath) {
	ColourScheme cs;
	memset(&cs, 0, sizeof(cs));
	initDefaultColourScheme(&cs);

	FontConfig font;
	memset(&font, 0, sizeof(font));
	snprintf(font.path, sizeof(font.path), "%s", "resources/fonts/console.ttf");
	font.size = 9;
	font.spacing = 1;

	loadThemeJson(path, &cs, &font);

	char tmp[MAXPATH];
	snprintf(tmp, sizeof(tmp), "%s", outPath ? outPath : ".tmp_files/theme_canon.json");
	if(!outPath) {
		mkdir(".tmp_files", 0755);
	}
	saveThemeJson(tmp, &cs, &font);
	if(!fileExists(tmp)) {
		printf("cannot write %s\n", tmp);
		return 1;
	}
	if(outPath) {
		printf("wrote %s\n", outPath);
	} else {
		char *buf = slurp(tmp, NULL);
		if(buf) {
			fputs(buf, stdout);
			free(buf);
		}
		unlink(tmp);
	}
	return 0;
}

/* --- tokens ------------------------------------------------------------- */

static int runTokens(void) {
	printf("%d canonical colour paths:\n", themePathCount());
	for(int i = 0; i < themePathCount(); i++) {
		printf("  %s\n", themePathName(i));
	}
	return 0;
}

/* --- selftest ----------------------------------------------------------- */

static void makeDirs(const char *path) {
	char cmd[512];
	snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", path);
	if(system(cmd) != 0) {
		printf("FAIL cannot create %s\n", path);
		g_failures++;
	}
}

static int runSelfTest(void) {
	mkdir(".tmp_files", 0755);
	makeDirs(".tmp_files/tt/xdg/spectrax");
	makeDirs(".tmp_files/tt/home/.config/spectrax");
	makeDirs(".tmp_files/tt/xdgdata/spectrax");
	makeDirs(".tmp_files/tt/homedata/.local/share/spectrax");
	writeText(".tmp_files/tt/xdg/spectrax/cfg.json", "{}");
	writeText(".tmp_files/tt/home/.config/spectrax/cfg.json", "{}");

	char out[MAXPATH];

	/* 1. --config-dir beats everything. */
	{
		char *av[] = { "theme_tool", "--config-dir", ".tmp_files/tt/xdg/spectrax", NULL };
		resolveConfigDir(3, av, out, sizeof(out));
		CHECK(strcmp(out, ".tmp_files/tt/xdg/spectrax") == 0,
		      "--config-dir wins over the XDG and HOME candidates");
	}

	/* 2. $XDG_CONFIG_HOME/spectrax wins over $HOME/.config/spectrax. */
	{
		setenv("XDG_CONFIG_HOME", ".tmp_files/tt/xdg", 1);
		setenv("HOME", ".tmp_files/tt/home", 1);
		char *av[] = { "theme_tool", NULL };
		resolveConfigDir(1, av, out, sizeof(out));
		CHECK(strcmp(out, ".tmp_files/tt/xdg/spectrax") == 0,
		      "XDG_CONFIG_HOME wins over HOME");
	}

	/* 3. An XDG dir without cfg.json falls through to HOME. */
	{
		unlink(".tmp_files/tt/xdg/spectrax/cfg.json");
		char *av[] = { "theme_tool", NULL };
		resolveConfigDir(1, av, out, sizeof(out));
		CHECK(strcmp(out, ".tmp_files/tt/home/.config/spectrax") == 0,
		      "HOME is the fallback when the XDG dir has no cfg.json");
		writeText(".tmp_files/tt/xdg/spectrax/cfg.json", "{}");
	}

	/* 4. With neither, the config dir is the cwd. */
	{
		unsetenv("XDG_CONFIG_HOME");
		setenv("HOME", ".tmp_files/tt/empty-home", 1);
		char *av[] = { "theme_tool", NULL };
		resolveConfigDir(1, av, out, sizeof(out));
		CHECK(strcmp(out, ".") == 0, "the cwd is the last resort");
	}

	/* 5. The data dir is gated on the DIRECTORY existing, not on
	 *    cfg.json -- the opposite rule to the config dir. */
	{
		setenv("XDG_DATA_HOME", ".tmp_files/tt/xdgdata", 1);
		setenv("HOME", ".tmp_files/tt/homedata", 1);
		char *av[] = { "theme_tool", NULL };
		resolveDataDir(1, av, out, sizeof(out));
		CHECK(strcmp(out, ".tmp_files/tt/xdgdata/spectrax") == 0,
		      "the data dir is $XDG_DATA_HOME/spectrax when it exists");
		rmdir(".tmp_files/tt/xdgdata/spectrax");
		resolveDataDir(1, av, out, sizeof(out));
		CHECK(strcmp(out, ".tmp_files/tt/homedata/.local/share/spectrax") == 0,
		      "the data dir falls back to $HOME/.local/share/spectrax");
	}

	/* 6. A legacy flat theme still loads, through the alias table. */
	{
		const char *flat = ".tmp_files/tt/flat.json";
		writeText(flat,
		          "{\"colors\":{\"background\":\"#010203FF\",\"outline\":\"#0A0B0CFF\","
		          "\"chipPalette3\":\"#101112FF\"}}");
		ColourScheme cs;
		memset(&cs, 0, sizeof(cs));
		FontConfig font = { { 0 }, 0, 0 };
		loadThemeJson(flat, &cs, &font);
		CHECK(cs.backgroundColor.r == 1 && cs.backgroundColor.g == 2 &&
		          cs.backgroundColor.b == 3, "legacy background loads");
		CHECK(cs.outlineColour.r == 0x0A, "legacy outline loads");
		CHECK(cs.chipPalette3.r == 0x10 && cs.chipPalette3.b == 0x12,
		      "legacy chipPalette3 loads");
	}

	/* 7. canon() writes the grouped schema-2 form, fills the gaps from
	 *    the defaults and drops the unknown keys. */
	{
		const char *mixed = ".tmp_files/tt/mixed.json";
		writeText(mixed,
		          "{\"schema\":2,\"colors\":{\"text\":{\"primary\":\"#010203FF\","
		          "\"nope\":\"#FFFFFF\"},\"chipPalette3\":\"#101112FF\"}}");
		ColourScheme cs;
		memset(&cs, 0, sizeof(cs));
		initDefaultColourScheme(&cs);
		FontConfig font = { { 0 }, 0, 0 };
		loadThemeJson(mixed, &cs, &font);
		const char *outp = ".tmp_files/tt/canon.json";
		saveThemeJson(outp, &cs, &font);
		CHECK(fileExists(outp), "canon writes the canonical form");
		char *buf = slurp(outp, NULL);
		CHECK(buf != NULL, "the canonical form is readable");
		if(buf) {
			cJSON *doc = cJSON_Parse(buf);
			CHECK(doc != NULL, "the canonical form parses");
			if(doc) {
				cJSON *schema = cJSON_GetObjectItemCaseSensitive(doc, "schema");
				CHECK(cJSON_IsNumber(schema) && schema->valueint == 2,
				      "the canonical form declares schema 2");
				cJSON *colors = cJSON_GetObjectItemCaseSensitive(doc, "colors");
				cJSON *text = cJSON_GetObjectItemCaseSensitive(colors, "text");
				CHECK(cJSON_IsObject(text), "the canonical form groups by use case");
				CHECK(cJSON_GetObjectItemCaseSensitive(text, "primary") != NULL,
				      "the canonical form writes text.primary");
				CHECK(cJSON_GetObjectItemCaseSensitive(text, "nope") == NULL,
				      "the canonical form drops the unknown key");
				cJSON *chip = cJSON_GetObjectItemCaseSensitive(colors, "chip");
				cJSON *pal = cJSON_GetObjectItemCaseSensitive(chip, "palette");
				CHECK(cJSON_IsArray(pal) && cJSON_GetArraySize(pal) == 8,
				      "the canonical form writes chip.palette as an array of 8");
				cJSON_Delete(doc);
			}
			free(buf);
		}
		/* A key the file omits keeps the seeded default, not zero. */
		Color *dim = themeFieldByName(&cs, "surface.dim");
		CHECK(dim && dim->a == 170, "an omitted key keeps the built-in default");
	}

	/* 8. check() reports a missing key, an unknown key and a zero-alpha
	 *    value instead of letting them through silently. */
	{
		const char *broken = ".tmp_files/tt/broken.json";
		writeText(broken,
		          "{\"colors\":{\"text\":{\"primary\":\"#010203FF\",\"nope\":\"#FFFFFF\"},"
		          "\"surface\":{\"window\":\"#00000000\"}}}");
		Audit a;
		CHECK(auditThemeFile(broken, &a), "the broken theme is readable");
		CHECK(a.unknownCount == 1, "check reports the unknown key");
		CHECK(a.zeroAlphaCount == 1, "check reports the zero-alpha value");
		CHECK(a.missingCount == themePathCount() - 2,
		      "check reports every key the file does not supply");
	}

	/* 9. bound(): a class nothing names is inert; an ancestor of a bound
	 *    class still contributes through the extends chain. */
	{
		const char *fixture = ".tmp_files/tt/bound.json";
		writeText(fixture,
		          "{\"styles\":{\"dial\":{},\"mid\":{\"extends\":\"dial\"},"
		          "\"leaf\":{\"extends\":\"mid\"},\"orphan\":{}},"
		          "\"layouts\":{\"row\":{\"childClasses\":[\"leaf\"]}}}");
		char *fb = slurp(fixture, NULL);
		cJSON *fd = fb ? cJSON_Parse(fb) : NULL;
		free(fb);
		CHECK(fd != NULL, "the bound fixture parses");
		if(fd) {
			ClassEntry cls[MAX_CLASSES];
			int n = collectClasses(fd, cls, MAX_CLASSES);
			CHECK(n == 4, "the bound fixture has 4 classes");
			markBound(fd, cls, n);
			markAncestors(cls, n);
			CHECK(cls[2].bound, "leaf is bound by the layout");
			CHECK(cls[1].ancestor, "mid is an ancestor of a bound class");
			CHECK(cls[0].ancestor, "dial is an ancestor of a bound class");
			CHECK(!cls[3].bound && !cls[3].ancestor, "orphan is bound by nothing");
			cJSON_Delete(fd);
		}
	}

	if(g_failures) {
		printf("%d theme-tool check(s) failed\n", g_failures);
		return 1;
	}
	printf("theme tool: all checks passed\n");
	return 0;
}

/* --- entry point -------------------------------------------------------- */

static void usage(void) {
	printf("usage: theme_tool <command> [args]\n"
	       "  where                       resolve the config/data dirs\n"
	       "  check <theme.json>          audit a theme file\n"
	       "  canon <theme.json> [-o F]   print/write the canonical grouped form\n"
	       "  bound <layout.json>         report which classes the config binds\n"
	       "  tokens                      list the canonical colour paths\n"
	       "  --selftest                  headless self-check\n");
}

int main(int argc, char **argv) {
	if(argc < 2) {
		usage();
		return 2;
	}
	const char *cmd = argv[1];
	if(strcmp(cmd, "--selftest") == 0) {
		return runSelfTest();
	}
	if(strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
		usage();
		return 0;
	}
	if(strcmp(cmd, "where") == 0) {
		return runWhere(argc - 1, argv + 1);
	}
	if(strcmp(cmd, "check") == 0) {
		if(argc < 3) {
			usage();
			return 2;
		}
		return runCheck(argv[2]);
	}
	if(strcmp(cmd, "canon") == 0) {
		if(argc < 3) {
			usage();
			return 2;
		}
		const char *out = NULL;
		for(int i = 3; i < argc - 1; i++) {
			if(strcmp(argv[i], "-o") == 0) {
				out = argv[i + 1];
			}
		}
		return runCanon(argv[2], out);
	}
	if(strcmp(cmd, "tokens") == 0) {
		return runTokens();
	}
	if(strcmp(cmd, "bound") == 0) {
		if(argc < 3) {
			usage();
			return 2;
		}
		return runBound(argv[2]);
	}
	printf("unknown command '%s'\n", cmd);
	usage();
	return 2;
}
