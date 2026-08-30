#include "config_io.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static int hexNibble(char c) {
	if(c >= '0' && c <= '9') return c - '0';
	if(c >= 'a' && c <= 'f') return c - 'a' + 10;
	if(c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

bool parseHexColor(const char *s, Color *out) {
	if(!s) {
		return false;
	}
	if(*s == '#') {
		s++;
	}
	size_t len = strlen(s);
	if(len != 6 && len != 8) {
		return false;
	}
	int nib[8];
	for(size_t i = 0; i < len; i++) {
		nib[i] = hexNibble(s[i]);
		if(nib[i] < 0) {
			return false;
		}
	}
	Color c;
	c.r = (unsigned char)((nib[0] << 4) | nib[1]);
	c.g = (unsigned char)((nib[2] << 4) | nib[3]);
	c.b = (unsigned char)((nib[4] << 4) | nib[5]);
	c.a = (len == 8) ? (unsigned char)((nib[6] << 4) | nib[7]) : 255;
	*out = c;
	return true;
}
