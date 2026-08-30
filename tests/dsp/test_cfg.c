#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "cJSON.h"
#include "io/config_io.h"
#include "paths.h"
#include "settings.h"
#include "theme.h"

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
	int failed = 0;
	char *argv[] = { "spectrax", "--data-dir", "/tmp/somewhere", NULL };
	char buf[512];
	resolveDataDir(3, argv, buf, sizeof(buf));
	if(strcmp(buf, "/tmp/somewhere") != 0) {
		printf("FAIL resolve --data-dir: got '%s'\n", buf);
		failed = 1;
	}
	/* Missing value: falls through to fallback logic.
	 * Snapshot HOME/XDG_DATA_HOME/XDG_CONFIG_HOME and unset all so the
	 * host's data dirs cannot silently satisfy the resolution. */
	char old_home2[512] = "";
	char old_xdg_data2[512] = "";
	char old_xdg_cfg2[512] = "";
	const char *h2 = getenv("HOME");
	const char *xd2 = getenv("XDG_DATA_HOME");
	const char *xc2 = getenv("XDG_CONFIG_HOME");
	if(h2) { strncpy(old_home2, h2, sizeof(old_home2) - 1); }
	if(xd2) { strncpy(old_xdg_data2, xd2, sizeof(old_xdg_data2) - 1); }
	if(xc2) { strncpy(old_xdg_cfg2, xc2, sizeof(old_xdg_cfg2) - 1); }
	unsetenv("HOME");
	unsetenv("XDG_DATA_HOME");
	unsetenv("XDG_CONFIG_HOME");

	char *argv2[] = { "spectrax", "--data-dir", NULL };
	resolveDataDir(2, argv2, buf, sizeof(buf));
	if(strcmp(buf, ".") != 0) {
		printf("FAIL resolve missing value: got '%s'\n", buf);
		failed = 1;
	}

	if(h2) setenv("HOME", old_home2, 1); else unsetenv("HOME");
	if(xd2) setenv("XDG_DATA_HOME", old_xdg_data2, 1); else unsetenv("XDG_DATA_HOME");
	if(xc2) setenv("XDG_CONFIG_HOME", old_xdg_cfg2, 1); else unsetenv("XDG_CONFIG_HOME");
	return failed;
}

static int test_resolve_data_dir_home(void) {
	int failed = 0;
	char old_home[512] = "";
	char old_xdg_data[512] = "";
	char old_xdg_cfg[512] = "";
	const char *h = getenv("HOME");
	const char *xd = getenv("XDG_DATA_HOME");
	const char *xc = getenv("XDG_CONFIG_HOME");
	if(h) { strncpy(old_home, h, sizeof(old_home) - 1); }
	if(xd) { strncpy(old_xdg_data, xd, sizeof(old_xdg_data) - 1); }
	if(xc) { strncpy(old_xdg_cfg, xc, sizeof(old_xdg_cfg) - 1); }

	/* $HOME/.local/share/spectrax absent -> cwd.
	 * The new data dir resolver uses XDG data home semantics:
	 * gates on directory existence, not cfg.json. */
	setenv("HOME", ".tmp_files/nonexistent_home", 1);
	unsetenv("XDG_DATA_HOME");
	unsetenv("XDG_CONFIG_HOME");
	char buf[512];
	resolveDataDir(1, (char *[]){"spectrax", NULL}, buf, sizeof(buf));
	if(strcmp(buf, ".") != 0) {
		printf("FAIL home no data dir: got '%s'\n", buf);
		failed = 1;
	}

	/* $HOME/.local/share/spectrax EXISTS -> that dir.
	 * Gate is dir-exists only — no cfg.json needed (and creating
	 * one would not change behaviour either, because the data-dir
	 * resolver doesn't read cfg.json). Build the nested path
	 * (.local/share/spectrax) under tmp_root via mkdir loop. */
	ensure_tmp_dirs();
	mkdir(".tmp_files/hometest", 0755);
	mkdir(".tmp_files/hometest/.local", 0755);
	mkdir(".tmp_files/hometest/.local/share", 0755);
	mkdir(".tmp_files/hometest/.local/share/spectrax", 0755);
	setenv("HOME", ".tmp_files/hometest", 1);
	resolveDataDir(1, (char *[]){"spectrax", NULL}, buf, sizeof(buf));
	if(strcmp(buf, ".tmp_files/hometest/.local/share/spectrax") != 0) {
		printf("FAIL home data dir exists: got '%s'\n", buf);
		failed = 1;
	}
	rmdir(".tmp_files/hometest/.local/share/spectrax");
	rmdir(".tmp_files/hometest/.local/share");
	rmdir(".tmp_files/hometest/.local");
	rmdir(".tmp_files/hometest");

	if(h) setenv("HOME", old_home, 1); else unsetenv("HOME");
	if(xd) setenv("XDG_DATA_HOME", old_xdg_data, 1); else unsetenv("XDG_DATA_HOME");
	if(xc) setenv("XDG_CONFIG_HOME", old_xdg_cfg, 1); else unsetenv("XDG_CONFIG_HOME");
	return failed;
}

static int test_resolve_config_dir_flag(void) {
	int failed = 0;
	/* --config-dir <dir> always wins, no env isolation needed. */
	char *argv[] = { "spectrax", "--config-dir", "/tmp/some-cfg", NULL };
	char buf[512];
	resolveConfigDir(3, argv, buf, sizeof(buf));
	if(strcmp(buf, "/tmp/some-cfg") != 0) {
		printf("FAIL resolve --config-dir: got '%s'\n", buf);
		failed = 1;
	}

	/* Missing value: falls through. Snapshot HOME/XDG_CONFIG_HOME
	 * and unset both so the host's config dirs cannot silently win. */
	char old_home[512] = "";
	char old_xdg[512] = "";
	const char *h = getenv("HOME");
	const char *x = getenv("XDG_CONFIG_HOME");
	if(h) { strncpy(old_home, h, sizeof(old_home) - 1); }
	if(x) { strncpy(old_xdg, x, sizeof(old_xdg) - 1); }
	unsetenv("HOME");
	unsetenv("XDG_CONFIG_HOME");

	char *argv2[] = { "spectrax", "--config-dir", NULL };
	resolveConfigDir(2, argv2, buf, sizeof(buf));
	if(strcmp(buf, ".") != 0) {
		printf("FAIL resolve config-dir missing value: got '%s'\n", buf);
		failed = 1;
	}

	if(h) setenv("HOME", old_home, 1); else unsetenv("HOME");
	if(x) setenv("XDG_CONFIG_HOME", old_xdg, 1); else unsetenv("XDG_CONFIG_HOME");
	return failed;
}

static int test_resolve_config_dir_home(void) {
	int failed = 0;
	char old_home[512] = "";
	char old_xdg[512] = "";
	const char *h = getenv("HOME");
	const char *x = getenv("XDG_CONFIG_HOME");
	if(h) { strncpy(old_home, h, sizeof(old_home) - 1); }
	if(x) { strncpy(old_xdg, x, sizeof(old_xdg) - 1); }
	unsetenv("XDG_CONFIG_HOME");

	/* $HOME/.config/spectrax absent (no cfg.json) -> cwd. */
	setenv("HOME", ".tmp_files/cfg_nonexistent_home", 1);
	char buf[512];
	resolveConfigDir(1, (char *[]){"spectrax", NULL}, buf, sizeof(buf));
	if(strcmp(buf, ".") != 0) {
		printf("FAIL config home absent: got '%s'\n", buf);
		failed = 1;
	}

	/* $HOME/.config/spectrax WITH cfg.json -> that dir. */
	ensure_tmp_dirs();
	mkdir(".tmp_files/cfgtest", 0755);
	mkdir(".tmp_files/cfgtest/.config", 0755);
	mkdir(".tmp_files/cfgtest/.config/spectrax", 0755);
	FILE *f = fopen(".tmp_files/cfgtest/.config/spectrax/cfg.json", "wb");
	if(f) { fputs("{}", f); fclose(f); }
	setenv("HOME", ".tmp_files/cfgtest", 1);
	resolveConfigDir(1, (char *[]){"spectrax", NULL}, buf, sizeof(buf));
	if(strcmp(buf, ".tmp_files/cfgtest/.config/spectrax") != 0) {
		printf("FAIL config home with cfg: got '%s'\n", buf);
		failed = 1;
	}

	/* Dir present but no cfg.json -> cwd (gate is cfg.json). */
	remove(".tmp_files/cfgtest/.config/spectrax/cfg.json");
	resolveConfigDir(1, (char *[]){"spectrax", NULL}, buf, sizeof(buf));
	if(strcmp(buf, ".") != 0) {
		printf("FAIL config home no cfg: got '%s'\n", buf);
		failed = 1;
	}

	rmdir(".tmp_files/cfgtest/.config/spectrax");
	rmdir(".tmp_files/cfgtest/.config");
	rmdir(".tmp_files/cfgtest");

	if(h) setenv("HOME", old_home, 1); else unsetenv("HOME");
	if(x) setenv("XDG_CONFIG_HOME", old_xdg, 1); else unsetenv("XDG_CONFIG_HOME");
	return failed;
}

static int test_resolve_config_dir_xdg(void) {
	int failed = 0;
	/* $XDG_CONFIG_HOME/spectrax wins over $HOME/.config/spectrax when
	 * the XDG candidate exists and has cfg.json. Gate is cfg.json. */
	char old_home[512] = "";
	char old_xdg_cfg[512] = "";
	const char *h = getenv("HOME");
	const char *x = getenv("XDG_CONFIG_HOME");
	if(h) { strncpy(old_home, h, sizeof(old_home) - 1); }
	if(x) { strncpy(old_xdg_cfg, x, sizeof(old_xdg_cfg) - 1); }

	ensure_tmp_dirs();
	mkdir(".tmp_files/xdgcfgtest", 0755);
	mkdir(".tmp_files/xdgcfgtest/spectrax", 0755);
	FILE *f = fopen(".tmp_files/xdgcfgtest/spectrax/cfg.json", "wb");
	if(f) { fputs("{}", f); fclose(f); }
	/* Even if HOME points at a valid config dir, XDG must win. */
	mkdir(".tmp_files/xdgcfgtest/home_with_cfg", 0755);
	mkdir(".tmp_files/xdgcfgtest/home_with_cfg/.config", 0755);
	mkdir(".tmp_files/xdgcfgtest/home_with_cfg/.config/spectrax", 0755);
	f = fopen(".tmp_files/xdgcfgtest/home_with_cfg/.config/spectrax/cfg.json", "wb");
	if(f) { fputs("{}", f); fclose(f); }
	setenv("HOME", ".tmp_files/xdgcfgtest/home_with_cfg", 1);
	setenv("XDG_CONFIG_HOME", ".tmp_files/xdgcfgtest", 1);

	char buf[512];
	resolveConfigDir(1, (char *[]){"spectrax", NULL}, buf, sizeof(buf));
	if(strcmp(buf, ".tmp_files/xdgcfgtest/spectrax") != 0) {
		printf("FAIL xdg config wins: got '%s'\n", buf);
		failed = 1;
	}

	remove(".tmp_files/xdgcfgtest/spectrax/cfg.json");
	rmdir(".tmp_files/xdgcfgtest/spectrax");
	remove(".tmp_files/xdgcfgtest/home_with_cfg/.config/spectrax/cfg.json");
	rmdir(".tmp_files/xdgcfgtest/home_with_cfg/.config/spectrax");
	rmdir(".tmp_files/xdgcfgtest/home_with_cfg/.config");
	rmdir(".tmp_files/xdgcfgtest/home_with_cfg");
	rmdir(".tmp_files/xdgcfgtest");

	if(h) setenv("HOME", old_home, 1); else unsetenv("HOME");
	if(x) setenv("XDG_CONFIG_HOME", old_xdg_cfg, 1); else unsetenv("XDG_CONFIG_HOME");
	return failed;
}

static int test_resolve_data_dir_xdg(void) {
	int failed = 0;
	/* $XDG_DATA_HOME/spectrax wins over $HOME/.local/share/spectrax when
	 * the XDG candidate directory exists. Gate is dir-exists only —
	 * no cfg.json needed (data dir resolver doesn't read cfg.json). */
	char old_home[512] = "";
	char old_xdg_data[512] = "";
	char old_xdg_cfg[512] = "";
	const char *h = getenv("HOME");
	const char *xd = getenv("XDG_DATA_HOME");
	const char *xc = getenv("XDG_CONFIG_HOME");
	if(h) { strncpy(old_home, h, sizeof(old_home) - 1); }
	if(xd) { strncpy(old_xdg_data, xd, sizeof(old_xdg_data) - 1); }
	if(xc) { strncpy(old_xdg_cfg, xc, sizeof(old_xdg_cfg) - 1); }

	ensure_tmp_dirs();
	mkdir(".tmp_files/xdgdatatest", 0755);
	mkdir(".tmp_files/xdgdatatest/spectrax", 0755);
	/* Even if HOME points at a valid data dir, XDG must win. */
	mkdir(".tmp_files/xdgdatatest/home_with_data", 0755);
	mkdir(".tmp_files/xdgdatatest/home_with_data/.local", 0755);
	mkdir(".tmp_files/xdgdatatest/home_with_data/.local/share", 0755);
	mkdir(".tmp_files/xdgdatatest/home_with_data/.local/share/spectrax", 0755);
	setenv("HOME", ".tmp_files/xdgdatatest/home_with_data", 1);
	setenv("XDG_DATA_HOME", ".tmp_files/xdgdatatest", 1);
	unsetenv("XDG_CONFIG_HOME");

	char buf[512];
	resolveDataDir(1, (char *[]){"spectrax", NULL}, buf, sizeof(buf));
	if(strcmp(buf, ".tmp_files/xdgdatatest/spectrax") != 0) {
		printf("FAIL xdg data wins: got '%s'\n", buf);
		failed = 1;
	}

	rmdir(".tmp_files/xdgdatatest/spectrax");
	rmdir(".tmp_files/xdgdatatest/home_with_data/.local/share/spectrax");
	rmdir(".tmp_files/xdgdatatest/home_with_data/.local/share");
	rmdir(".tmp_files/xdgdatatest/home_with_data/.local");
	rmdir(".tmp_files/xdgdatatest/home_with_data");
	rmdir(".tmp_files/xdgdatatest");

	if(h) setenv("HOME", old_home, 1); else unsetenv("HOME");
	if(xd) setenv("XDG_DATA_HOME", old_xdg_data, 1); else unsetenv("XDG_DATA_HOME");
	if(xc) setenv("XDG_CONFIG_HOME", old_xdg_cfg, 1); else unsetenv("XDG_CONFIG_HOME");
	return failed;
}

static int test_resolve_config_dir_truncation(void) {
	int failed = 0;
	/* Truncation guard: --config-dir with a value longer than `out`
	 * must leave `out` untouched (caller can fall back). */
	char *argv[] = { "spectrax", "--config-dir", "/this/path/is/definitely/longer/than/our/tiny/buffer", NULL };
	char buf[16];
	strcpy(buf, "PRESERVE_VALUE");
	resolveConfigDir(3, argv, buf, sizeof(buf));
	if(strcmp(buf, "PRESERVE_VALUE") != 0) {
		printf("FAIL config-dir truncation: got '%s', expected untouched\n", buf);
		failed = 1;
	}
	return failed;
}

static void writeFixture(const char *path, const char *content) {
	FILE *f = fopen(path, "wb");
	if(f) {
		fputs(content, f);
		fclose(f);
	}
}

static int test_theme_parse_full(void) {
	ensure_tmp_dirs();
	writeFixture(".tmp_files/theme_full.json",
		"{\n"
		"  \"font\": { \"path\": \"resources/fonts/console.ttf\", \"size\": 9, \"spacing\": 1 },\n"
		"  \"colors\": {\n"
		"    \"background\": \"#cf6e3a\", \"label\": \"#c8b4b4\", \"dial\": \"#ff0000\",\n"
		"    \"arrangerCellText\": \"#c8b4b4\"\n"
		"  }\n"
		"}\n");
	ColourScheme cs = { 0 };
	FontConfig font = { { 0 }, 0, 0 };
	loadThemeJson(".tmp_files/theme_full.json", &cs, &font);
	if(cs.label.r != 200 || cs.label.g != 180 || cs.dial.r != 255 ||
	   cs.backgroundColor.r != 207 || cs.arrangerCellText.b != 180) {
		printf("FAIL theme colours\n");
		return 1;
	}
	if(strcmp(font.path, "resources/fonts/console.ttf") != 0 || font.size != 9 || font.spacing != 1) {
		printf("FAIL theme font\n");
		return 1;
	}
	return 0;
}

static int test_theme_parse_partial(void) {
	ensure_tmp_dirs();
	writeFixture(".tmp_files/theme_partial.json",
		"{ \"colors\": { \"dial\": \"#00ff00\" } }\n");
	/* Pre-fill with defaults; only the dial key present -> everything else stays default. */
	ColourScheme cs = { 0 };
	cs.label = (Color){ 200, 180, 180, 255 };
	cs.dial = (Color){ 255, 0, 0, 255 };
	FontConfig font = { { 0 }, 9, 1 };
	loadThemeJson(".tmp_files/theme_partial.json", &cs, &font);
	if(cs.dial.r != 0 || cs.dial.g != 255 || cs.label.r != 200) {
		printf("FAIL theme partial override\n");
		return 1;
	}
	if(font.size != 9) {
		printf("FAIL theme font unchanged when absent\n");
		return 1;
	}
	return 0;
}

static int test_theme_missing_file(void) {
	ColourScheme cs = { 0 };
	cs.label = (Color){ 1, 2, 3, 4 };
	Color before = cs.label;
	FontConfig font = { { 0 }, 9, 1 };
	loadThemeJson(".tmp_files/no_such_theme.json", &cs, &font);
	if(cs.label.r != before.r || cs.label.g != before.g) {
		printf("FAIL theme missing file must not mutate\n");
		return 1;
	}
	return 0;
}

static int test_theme_roundtrip(void) {
	ensure_tmp_dirs();
	ColourScheme cs = { 0 };
	cs.backgroundColor = (Color){ 207, 110, 58, 255 };
	cs.label = (Color){ 200, 180, 180, 255 };
	cs.dial = (Color){ 12, 34, 56, 78 };
	FontConfig font = { "myfont.ttf", 12, 2 };
	saveThemeJson(".tmp_files/theme_rt.json", &cs, &font);
	ColourScheme cs2 = { 0 };
	FontConfig font2 = { { 0 }, 0, 0 };
	loadThemeJson(".tmp_files/theme_rt.json", &cs2, &font2);
	if(cs2.label.r != 200 || cs2.label.g != 180 || cs2.dial.a != 78 ||
	   cs2.backgroundColor.r != 207 || strcmp(font2.path, "myfont.ttf") != 0 ||
	   font2.size != 12 || font2.spacing != 2) {
		printf("FAIL theme roundtrip\n");
		return 1;
	}
	return 0;
}

static int test_settings_parse_full(void) {
	ensure_tmp_dirs();
	writeFixture(".tmp_files/cfg_full.json",
		"{ \"defaultBPM\": 120, \"enabledChannels\": 8, \"defaultSequenceLength\": 16,\n"
		"  \"defaultVoiceCount\": 1, \"voiceTypes\": [4,1,2,1,2,1,2,2], \"theme\": \"clr.json\" }\n");
	Settings s = { 0 };
	char theme[256];
	loadSettingsJson(".tmp_files/cfg_full.json", &s, theme, sizeof(theme));
	if(s.defaultBPM != 120 || s.enabledChannels != 8 || s.defaultSequenceLength != 16 ||
	   s.defaultVoiceCount != 1 || s.voiceTypes[0] != 4 || s.voiceTypes[1] != 1 ||
	   s.voiceTypes[7] != 2 || strcmp(theme, "clr.json") != 0) {
		printf("FAIL settings full\n");
		return 1;
	}
	return 0;
}

static int test_settings_partial(void) {
	ensure_tmp_dirs();
	writeFixture(".tmp_files/cfg_partial.json", "{ \"theme\": \"clr.json\" }\n");
	Settings s = { 0 };
	s.defaultBPM = 77;
	s.enabledChannels = 3;
	char theme[256];
	loadSettingsJson(".tmp_files/cfg_partial.json", &s, theme, sizeof(theme));
	if(s.defaultBPM != 77 || s.enabledChannels != 3) {
		printf("FAIL settings partial must keep defaults\n");
		return 1;
	}
	if(strcmp(theme, "clr.json") != 0) {
		printf("FAIL settings theme default\n");
		return 1;
	}
	return 0;
}

static int test_settings_missing(void) {
	Settings s = { 0 };
	s.defaultBPM = 99;
	char theme[256];
	loadSettingsJson(".tmp_files/no_such_cfg.json", &s, theme, sizeof(theme));
	if(s.defaultBPM != 99) {
		printf("FAIL settings missing file must not mutate\n");
		return 1;
	}
	return 0;
}

static int test_settings_roundtrip(void) {
	ensure_tmp_dirs();
	Settings s = { 0 };
	s.defaultBPM = 130;
	s.enabledChannels = 4;
	s.defaultSequenceLength = 32;
	s.defaultVoiceCount = 2;
	s.voiceTypes[0] = 4; s.voiceTypes[1] = 1; s.voiceTypes[2] = 2; s.voiceTypes[3] = 1;
	saveSettingsJson(".tmp_files/cfg_rt.json", &s, "clr.json");
	Settings s2 = { 0 };
	char theme[256];
	loadSettingsJson(".tmp_files/cfg_rt.json", &s2, theme, sizeof(theme));
	if(s2.defaultBPM != 130 || s2.enabledChannels != 4 || s2.defaultSequenceLength != 32 ||
	   s2.defaultVoiceCount != 2 || s2.voiceTypes[2] != 2 || strcmp(theme, "clr.json") != 0) {
		printf("FAIL settings roundtrip\n");
		return 1;
	}
	return 0;
}

int main(void) {
	int failed = 0;
	failed |= test_cjson_parse_smoke();
	failed |= test_hex_rgb();
	failed |= test_hex_invalid();
	failed |= test_resolve_data_dir_flag();
	failed |= test_resolve_data_dir_home();
	failed |= test_resolve_config_dir_flag();
	failed |= test_resolve_config_dir_home();
	failed |= test_resolve_config_dir_truncation();
	failed |= test_resolve_config_dir_xdg();
	failed |= test_resolve_data_dir_xdg();
	failed |= test_theme_parse_full();
	failed |= test_theme_parse_partial();
	failed |= test_theme_missing_file();
	failed |= test_theme_roundtrip();
	failed |= test_settings_parse_full();
	failed |= test_settings_partial();
	failed |= test_settings_missing();
	failed |= test_settings_roundtrip();
	if(failed) {
		printf("test_cfg: FAILURES\n");
		return 1;
	}
	printf("test_cfg: all tests passed\n");
	return 0;
}
