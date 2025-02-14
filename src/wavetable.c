#include "wavetable.h"

WavetablePool* createWavetablePool(){
    printf("creating wavetable pool\n");

    WavetablePool* wtp = (WavetablePool*)malloc(sizeof(WavetablePool));
    if(!wtp){
        printf("Could not allocate memory for WavetablePool.\n");
        return NULL;
    }
    wtp->data = (char*)malloc(MAX_WTPOOL_BYTES);
    if(!wtp->data){
        free(wtp);
        printf("Could not allocate memory for WavetablePool->data.\n");
        return NULL;
    }

    wtp->memoryUsed = 0;
    wtp->tableCount = 0;
    wtp->tables = malloc(sizeof(Wavetable*) * MAX_WAVETABLES);
    if(!wtp->tables){
        free(wtp->data);
        free(wtp);
        printf("Could not allocate memory for WavetablePool->tables.\n");
        return NULL;
    }

	printf("\t-> DONE.\n");
    return wtp;
}

void freeWavetablePool(WavetablePool* wtp){
    if(!wtp) return;
    for(int i = 0; i < wtp->tableCount; i++){
        free(wtp->tables[i]);
    }
    free(wtp->tables);
    free(wtp->data);
    free(wtp);
}

void loadWavetable(WavetablePool* wtp, char* name, float* data, size_t length){
    if(wtp->tableCount > MAX_WAVETABLES){
        printf("Max WT count reached\n");
        return;
    }

    size_t dataSize = sizeof(float)* length;
    float* wtData = (float*)((char*)wtp->data + wtp->memoryUsed);
    memcpy(wtData, data, dataSize);

    Wavetable* wt = (Wavetable*)malloc(sizeof(Wavetable));
    if(!wt) return;

    wt->data = wtData;
    wt->length = length;
    wt->name = name;
    if (!wt->name) {
        free(wt);
        return;
    }

    wtp->tables[wtp->tableCount] = wt;
    wtp->tableCount++;
    wtp->memoryUsed += dataSize;
}
