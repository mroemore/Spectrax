#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "cJSON.h"
#include "io/config_io.h"
#include "paths.h"

/* Local copy of test_io.c's ensure_tmp_dirs() — creates .tmp_files/ if
 * missing. test_cfg.c doesn't share a translation unit with test_io.c,
 * so the helper must be replicated here. */
static void ensure_tmp_dirs(void) {
	mkdir(".tmp_files", 0755);
}

static int test_cjson_parse_smoke(void) {
	cJSON *doc = cJSON_Parse("{\"a\": 1, \"b\": [true, \"x\"]}");
	if(!doc) {
		printf("FAIL cjson parse: %s\n", cJSON_GetErrorPtr());
		return 1;
	}
	int failed = 0;
	cJSON *a = cJSON_GetObjectItemCaseSensitive(doc, "a");
	if(!a || a->valueint != 1) {
		printf("FAIL cjson int\n");
		failed = 1;
	}
	cJSON *b = cJSON_GetObjectItemCaseSensitive(doc, "b");
	if(!b || !cJSON_IsArray(b) || cJSON_GetArraySize(b) != 2) {
		printf("FAIL cjson array\n");
		failed = 1;
	}
	cJSON_Delete(doc);
	return failed;
}

static int test_hex_rgb(void) {
	Color c;
	if(!parseHexColor("#ff0000", &c) || c.r != 255 || c.g != 0 || c.b != 0 || c.a != 255) {
		printf("FAIL hex #rrggbb\n");
		return 1;
	}
	if(!parseHexColor("#00ff00ff", &c) || c.g != 255 || c.a != 255) {
		printf("FAIL hex #rrggbbaa\n");
		return 1;
	}
	if(!parseHexColor("112233", &c) || c.r != 17 || c.g != 34 || c.b != 51) {
		printf("FAIL hex bare rrggbb\n");
		return 1;
	}
	if(!parseHexColor("#ABCDEF", &c) || c.r != 0xAB || c.g != 0xCD || c.b != 0xEF) {
		printf("FAIL hex uppercase\n");
		return 1;
	}
	return 0;
}

static int test_hex_invalid(void) {
	Color c = { 1, 2, 3, 4 };
	Color before = c;
	if(parseHexColor("#12345", &c) || parseHexColor("#gggggg", &c) ||
	   parseHexColor("123", &c) || parseHexColor("#123456789", &c) ||
	   parseHexColor("", &c) || parseHexColor(NULL, &c)) {
		printf("FAIL hex should reject malformed\n");
		return 1;
	}
	if(c.r != before.r || c.g != before.g || c.b != before.b || c.a != before.a) {
		printf("FAIL hex must not mutate on error\n");
		return 1;
	}
	return 0;
}

static int test_resolve_data_dir_flag(void) {
	char *argv[] = { "spectrax", "--data-dir", "/tmp/somewhere", NULL };
	char buf[512];
	resolveDataDir(3, argv, buf, sizeof(buf));
	if(strcmp(buf, "/tmp/somewhere") != 0) {
		printf("FAIL resolve --data-dir: got '%s'\n", buf);
		return 1;
	}
	/* Missing value: falls through to fallback logic.
	 * Snapshot HOME/XDG_CONFIG_HOME and unset both so the host's
	 * ~/.config/spectrax/cfg.json cannot silently satisfy the
	 * resolution. */
	char old_home2[512] = "";
	char old_xdg2[512] = "";
	const char *h2 = getenv("HOME");
	const char *x2 = getenv("XDG_CONFIG_HOME");
	if(h2) { strncpy(old_home2, h2, sizeof(old_home2) - 1); }
	if(x2) { strncpy(old_xdg2, x2, sizeof(old_xdg2) - 1); }
	unsetenv("HOME");
	unsetenv("XDG_CONFIG_HOME");

	char *argv2[] = { "spectrax", "--data-dir", NULL };
	resolveDataDir(2, argv2, buf, sizeof(buf));
	if(strcmp(buf, ".") != 0) {
		printf("FAIL resolve missing value: got '%s'\n", buf);
		return 1;
	}

	if(h2) setenv("HOME", old_home2, 1); else unsetenv("HOME");
	if(x2) setenv("XDG_CONFIG_HOME", old_xdg2, 1); else unsetenv("XDG_CONFIG_HOME");
	return 0;
}

static int test_resolve_data_dir_home(void) {
	char old_home[512] = "";
	char old_xdg[512] = "";
	const char *h = getenv("HOME");
	const char *x = getenv("XDG_CONFIG_HOME");
	if(h) { strncpy(old_home, h, sizeof(old_home) - 1); }
	if(x) { strncpy(old_xdg, x, sizeof(old_xdg) - 1); }

	/* $HOME/.config/spectrax with no cfg.json -> cwd */
	setenv("HOME", ".tmp_files/nonexistent_home", 1);
	unsetenv("XDG_CONFIG_HOME");
	char buf[512];
	resolveDataDir(1, (char *[]){"spectrax", NULL}, buf, sizeof(buf));
	if(strcmp(buf, ".") != 0) {
		printf("FAIL home no cfg: got '%s'\n", buf);
		return 1;
	}

	/* $HOME/.config/spectrax WITH cfg.json -> that dir.
	 * Implementation builds "$HOME/.config/spectrax" and checks for
	 * cfg.json inside it; so HOME=tmp_root gives a config dir at
	 * tmp_root/.config/spectrax, and we drop cfg.json there. */
	ensure_tmp_dirs();
	mkdir(".tmp_files/hometest", 0755);
	mkdir(".tmp_files/hometest/.config", 0755);
	mkdir(".tmp_files/hometest/.config/spectrax", 0755);
	FILE *f = fopen(".tmp_files/hometest/.config/spectrax/cfg.json", "wb");
	if(f) { fputs("{}", f); fclose(f); }
	setenv("HOME", ".tmp_files/hometest", 1);
	resolveDataDir(1, (char *[]){"spectrax", NULL}, buf, sizeof(buf));
	if(strcmp(buf, ".tmp_files/hometest/.config/spectrax") != 0) {
		printf("FAIL home with cfg: got '%s'\n", buf);
		return 1;
	}
	remove(".tmp_files/hometest/.config/spectrax/cfg.json");
	rmdir(".tmp_files/hometest/.config/spectrax");
	rmdir(".tmp_files/hometest/.config");
	rmdir(".tmp_files/hometest");

	if(h) setenv("HOME", old_home, 1); else unsetenv("HOME");
	if(x) setenv("XDG_CONFIG_HOME", old_xdg, 1); else unsetenv("XDG_CONFIG_HOME");
	return 0;
}

int main(void) {
	int failed = 0;
	failed |= test_cjson_parse_smoke();
	failed |= test_hex_rgb();
	failed |= test_hex_invalid();
	failed |= test_resolve_data_dir_flag();
	failed |= test_resolve_data_dir_home();
	if(failed) {
		printf("test_cfg: FAILURES\n");
		return 1;
	}
	printf("test_cfg: all tests passed\n");
	return 0;
}
