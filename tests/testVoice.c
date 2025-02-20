#include <stddef.h>
#include <stdint.h>

#include "raylib.h";
#include "../src/voice.h";
#include "../src/io.h";
#include "../src/oscillator.h";

#define SCREEN_W 1280
#define SCREEN_H 720

typedef struct {
    float* data;
    size_t size;
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
    Color c;
} WaveGraph;

WaveGraph* createWaveGraph(size_t size, uint16_t x, uint16_t y, uint16_t w, uint16_t h, Color c){
    WaveGraph* g = (WaveGraph*)malloc(sizeof(WaveGraph));
    if(!g){
        printf("could not allocate memory for Graph.\n");
        return NULL;
    }
    g->data = malloc(sizeof(float) * size);
    if(!g->data){
        printf("could not allocate memory for Graph data.\n");
        free(g);
        return NULL;
    }
    g->size = size;
    g->x = x;
    g->y = y;
    g->w = w;
    g->h = h;
    g->c = c;
    
    return g;
}

void drawWaveGraph(WaveGraph* g){
    DrawRectangleLines(g->x, g->y, g->w, g->h, g->c);
    int offset = g->y + g->h / 2;
    float increment = (float)g->w / (g->size - 1);

    for (size_t i = 0; i < g->size - 1; i++) {
        int x1 = g->x + (int)(i * increment);
        int x2 = g->x + (int)((i + 1) * increment);
        int y1 = offset - (int)(g->data[i] * (g->h / 2));
        int y2 = offset - (int)(g->data[i + 1] * (g->h / 2));
        DrawLine(x1, y1, x2, y2, g->c);
    }
}

int main(void){
    InitWindow(SCREEN_W, SCREEN_H, "Voice Test");
    SetTargetFPS(60);

    WaveGraph* g = createWaveGraph(1024, 10, 10, SCREEN_W - 20, 60, RED);
    if(!g) return 0;
    Settings* s = createSettings();
    SamplePool* sp = createSamplePool();
    loadSamplesfromDirectory("samples/", sp);
    WavetablePool* wtp = createWavetablePool();
    VoiceManager* vm = createVoiceManager(s, sp, wtp);
    // Instrument* i;
    // init_instrument(&i, VOICE_TYPE_SAMPLE, sp);



    for(int i = 0; i < 1024; i++){
        OutVal o = generateVoice(vm, vm->voicePools[1][0], i, 440.0f);
        g->data[i] = o.L;
    }

    while(!WindowShouldClose()){
        BeginDrawing();
        drawWaveGraph(g);
        EndDrawing();
    }

    CloseWindow();
}