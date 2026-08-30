#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "io/config_io.h"

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

int main(void) {
	int failed = 0;
	failed |= test_cjson_parse_smoke();
	failed |= test_hex_rgb();
	failed |= test_hex_invalid();
	if(failed) {
		printf("test_cfg: FAILURES\n");
		return 1;
	}
	printf("test_cfg: all tests passed\n");
	return 0;
}
