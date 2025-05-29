#ifndef SETTINGSIO_H
#define SETTINGSIO_H

#include "../io.h"
#include "../settings.h"

FileResult loadSettings(const char *filename, Settings *settings);
FileResult saveSettings(const char *filename, Settings *settings);

#endif
