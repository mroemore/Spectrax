/* libFuzzer harness for the JSON config loaders (src/io/config_io.c).
 *
 * Feeds one input to both loadSettingsJson() and loadThemeJson() through an
 * anonymous memfd (nothing touches disk). Targets untrusted settings/theme
 * files: cJSON parsing, the per-key overrides, and the hex-colour / theme-key
 * mapping. Run under ASan/UBSan. */

#define _GNU_SOURCE

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "config_io.h"
#include "settings.h"
#include "theme.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	int fd = memfd_create("spx_fuzz_cfg", MFD_CLOEXEC);
	if(fd < 0) {
		return 0;
	}
	if(size > 0 && write(fd, data, size) != (ssize_t)size) {
		close(fd);
		return 0;
	}
	if(lseek(fd, 0, SEEK_SET) < 0) {
		close(fd);
		return 0;
	}

	char path[64];
	snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);

	Settings settings;
	memset(&settings, 0, sizeof(settings));
	char themeFile[128];
	memset(themeFile, 0, sizeof(themeFile));
	loadSettingsJson(path, &settings, themeFile, sizeof(themeFile));

	ColourScheme cs;
	memset(&cs, 0, sizeof(cs));
	FontConfig font;
	memset(&font, 0, sizeof(font));
	loadThemeJson(path, &cs, &font);

	close(fd);
	return 0;
}
