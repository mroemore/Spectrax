#ifndef PATHS_H
#define PATHS_H

#include <stdbool.h>
#include <stddef.h>

void resolveDataDir(int argc, char **argv, char *out, size_t outsz);
bool chdirToDataDir(const char *base);

#endif