#include "wavetable.h"
#include "verify.h"

WavetablePool *createWavetablePool() {
	// printf("creating wavetable pool\n");

	WavetablePool *wtp = (WavetablePool *)malloc(sizeof(WavetablePool));
	if(!wtp) {
		printf("Could not allocate memory for WavetablePool.\n");
		return NULL;
	}
	wtp->data = (char *)malloc(MAX_WTPOOL_BYTES);
	if(!wtp->data) {
		free(wtp);
		printf("Could not allocate memory for WavetablePool->data.\n");
		return NULL;
	}

	wtp->memoryUsed = 0;
	wtp->tableCount = 0;
	wtp->tables = malloc(sizeof(Wavetable *) * MAX_WAVETABLES);
	if(!wtp->tables) {
		free(wtp->data);
		free(wtp);
		printf("Could not allocate memory for WavetablePool->tables.\n");
		return NULL;
	}

	// printf("\t-> DONE.\n");
	return wtp;
}

void freeWavetablePool(WavetablePool *wtp) {
	if(!wtp) return;
	/* tableCount is only ever incremented in loadWavetable; this documents
	 * the bound the free loop below relies on. */
	assert(wtp->tableCount >= 0 && wtp->tableCount <= MAX_WAVETABLES);
	for(int i = 0; i < wtp->tableCount; i++) {
		free(wtp->tables[i]);
	}
	free(wtp->tables);
	free(wtp->data);
	free(wtp);
}

void loadWavetable(WavetablePool *wtp, char *name, float *data, size_t length) {
	/* Preconditions (see wavetable.h). All callers pass a live pool and a
	 * non-null name today; a broken caller should fail loudly, not corrupt
	 * the pool's arena. */
	assert(wtp != NULL);
	assert(wtp->data != NULL);
	assert(wtp->tables != NULL);
	assert(data != NULL);

	/* Capacity is MAX_WAVETABLES slots; reject the (MAX+1)th load. The old
	 * '>' allowed tableCount to reach MAX and then wrote tables[MAX] out of
	 * bounds (see TESTING.md I5). */
	if(wtp->tableCount >= MAX_WAVETABLES) {
		printf("Max WT count reached\n");
		return;
	}

	size_t dataSize = sizeof(float) * length;
	/* The pool's arena is a fixed MAX_WTPOOL_BYTES block; refuse a load that
	 * would run past the end instead of overflowing it (TESTING.md I5). */
	if(wtp->memoryUsed + dataSize > (size_t)MAX_WTPOOL_BYTES) {
		printf("Wavetable pool exhausted\n");
		return;
	}

	float *wtData = (float *)((char *)wtp->data + wtp->memoryUsed);
	memcpy(wtData, data, dataSize);

	Wavetable *wt = (Wavetable *)malloc(sizeof(Wavetable));
	if(!wt) return;

	wt->data = wtData;
	wt->length = length;
	wt->name = name;
	if(!wt->name) {
		free(wt);
		return;
	}

	wtp->tables[wtp->tableCount] = wt;
	wtp->tableCount++;
	wtp->memoryUsed += dataSize;
}
