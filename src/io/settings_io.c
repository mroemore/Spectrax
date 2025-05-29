#include "settings_io.h"

FileResult saveSettings(const char *filename, Settings *settings) {
	FILE *file = fopen(filename, "wb");
	if(!file) return FILE_ERROR_OPEN;

	const char magic[] = "SET1";
	fwrite(magic, 1, 4, file);
	fwrite(settings, sizeof(Settings), 1, file);

	fclose(file);
	return FILE_OK;
}

FileResult loadSettings(const char *filename, Settings *settings) {
	FILE *file = fopen(filename, "rb");
	if(!file) return FILE_ERROR_OPEN;

	char magic[4];
	if(fread(magic, 1, 4, file) != 4 || memcmp(magic, "SET1", 4) != 0) {
		fclose(file);
		return FILE_ERROR_FORMAT;
	}

	if(fread(settings, sizeof(Settings), 1, file) != 1) {
		fclose(file);
		return FILE_ERROR_READ;
	}

	fclose(file);
	return FILE_OK;
}
