#ifndef GUIIO_H
#define GUIIO_H

#include "../io.h"
#include "../gui.h"

FileResult saveColourScheme(const char *filename, ColourScheme *colourScheme);
FileResult loadColourScheme(const char *filename, ColourScheme *colourScheme);
FileResult loadColourSchemeTxt(const char *filename, Color *colourArray[], int arraySize);

#endif
