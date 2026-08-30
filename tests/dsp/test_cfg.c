#include <stdio.h>
#include <string.h>
#include "cJSON.h"

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

int main(void) {
	int failed = 0;
	failed |= test_cjson_parse_smoke();
	if(failed) {
		printf("test_cfg: FAILURES\n");
		return 1;
	}
	printf("test_cfg: all tests passed\n");
	return 0;
}
