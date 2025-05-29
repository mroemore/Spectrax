#include "gui_io.h"

FileResult saveColourScheme(const char *filename, ColourScheme *colourScheme) {
	FILE *file = fopen(filename, "wb");
	if(!file) return FILE_ERROR_OPEN;

	const char magic[] = "CSC1";
	fwrite(magic, 1, 4, file);
	fwrite(colourScheme, sizeof(ColourScheme), 1, file);

	fclose(file);
	return FILE_OK;
}

FileResult loadColourScheme(const char *filename, ColourScheme *colourScheme) {
	FILE *file = fopen(filename, "rb");
	if(!file) return FILE_ERROR_OPEN;

	char magic[4];
	if(fread(magic, 1, 4, file) != 4 || memcmp(magic, "CSC1", 4) != 0) {
		fclose(file);
		return FILE_ERROR_FORMAT;
	}

	if(fread(colourScheme, sizeof(ColourScheme), 1, file) != 1) {
		fclose(file);
		return FILE_ERROR_READ;
	}

	fclose(file);
	return FILE_OK;
}

FileResult loadColourSchemeTxt(const char *filename, Color *colourArray[], int arraySize) {
	FILE *file = fopen(filename, "rb");
	if(!file) return FILE_ERROR_OPEN;

	char line[32];
	for(int i = 0; i < arraySize; i++) {
		if(!fgets(line, sizeof(line), file)) {
			fclose(file);
			return FILE_ERROR_READ;
		}

		int r, g, b;
		if(sscanf(line, "%d,%d,%d", &r, &g, &b) != 3 ||
		   r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
			fclose(file);
			return FILE_ERROR_FORMAT;
		}

		colourArray[i]->r = r;
		colourArray[i]->g = g;
		colourArray[i]->b = b;
	}

	fclose(file);
	return FILE_OK;
}
