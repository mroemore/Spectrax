#include "raylib.h"
#include "modsystem.h"

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600
#define HISTORY_SIZE 400

typedef struct {
    float values[HISTORY_SIZE];
    int current;
} SignalHistory;

void updateHistory(SignalHistory* history, float value) {
    history->values[history->current] = value;
    history->current = (history->current + 1) % HISTORY_SIZE;
}

void drawSignal(SignalHistory* history, int yOffset, float height, Color color) {
    for (int i = 0; i < HISTORY_SIZE - 1; i++) {
        int idx1 = (history->current + i) % HISTORY_SIZE;
        int idx2 = (history->current + i + 1) % HISTORY_SIZE;
        
        DrawLine(
            i,  yOffset + height - (history->values[idx1] * height),
            i + 1, yOffset + height - (history->values[idx2] * height),
            color
        );
    }
}

int main() {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "LFO Visualization");
    SetTargetFPS(60);
    
    initModSystem();
    ModList* modList = createModList();
    
    // Create LFOs
    LFO* bassLFO = createLFO(modList, 0, 0.12f, LS_SIN);
    LFO* midLFO = createLFO(modList, 1, 0.35f, LS_SIN);
    Random* fastLFO = createRandom(modList, 2, .1f, RT_SNH);
    LFO* ultraLFO = createLFO(modList, 3, 4.0f, LS_SIN);
	Envelope* adsrEnv = createADSR(modList, 0.5f, 1.2f, 1.5f, 1.1f);

    Parameter* volume = createParameter("vol", 1.0f, 0.0f, 1.0f);
    // Setup modulation routing
    addModulation(&bassLFO->base, midLFO->rate, 0.3f, MO_ADD);
    addModulation(&midLFO->base, fastLFO->rate, 0.5f, MO_ADD);
    //addModulation(&fastLFO->base, ultraLFO->rate, 0.52f, MOD_MUL);
    //addModulation(&ultraLFO->base, fastLFO->rate, 1.71f, MOD_ADD);
    addModulation(&ultraLFO->base, adsrEnv->base.output, .5f, MO_MUL);
    addModulation(&adsrEnv->base, volume, 1.0f, MO_MUL);


	adsrEnv->isTriggered = true;

    SignalHistory histories[6] = {0};

    while (!WindowShouldClose()) {
        float deltaTime = GetFrameTime();
        
        processModulations(modList, deltaTime);

        // Update histories with proper Parameter access
        updateHistory(&histories[0], getParameterValue(bassLFO->base.output));
        updateHistory(&histories[1], getParameterValue(midLFO->base.output));
        updateHistory(&histories[2], getParameterValue(fastLFO->base.output));
        updateHistory(&histories[3], getParameterValue(ultraLFO->base.output));
        updateHistory(&histories[4], getParameterValue(adsrEnv->base.output));
        updateHistory(&histories[5], getParameterValue(volume));

        BeginDrawing();
        ClearBackground(BLACK);

        // Draw oscilloscope-style visualizations
        drawSignal(&histories[0], 50, 30.0f, RED);     // Bass LFO
		drawSignal(&histories[1], 100, 30.0f, GREEN);   // Mid LFO
		drawSignal(&histories[2], 150, 30.0f, BLUE);    // Fast LFO
		drawSignal(&histories[3], 200, 30.0f, YELLOW);  // Ultra LFO
		drawSignal(&histories[4], 250, 30.0f, PINK); 
		drawSignal(&histories[5], 300, 30.0f, BROWN); 

        // Draw labels and modulation info
        DrawText("Bass LFO (0.1 Hz)", 10, 40, 20, RED);
        DrawText("Mid LFO (0.5 Hz + Bass mod)", 10, 90, 20, GREEN);
        DrawText("Fast Square (2.0 Hz + Mid/Ultra mod)", 10, 140, 20, BLUE);
        DrawText("Ultra LFO (4.0 Hz + Fast mod)", 10, 190, 20, YELLOW);
        DrawText("ADSR", 10, 240, 20, PINK);

        // Update rate display
        DrawText(TextFormat("Bass Rate: %.2f", getParameterValue(bassLFO->rate)), 600, 40, 20, RED);
        DrawText(TextFormat("Mid Rate: %.2f", getParameterValue(midLFO->rate)), 600, 90, 20, GREEN);
        DrawText(TextFormat("Fast Rate: %.2f", getParameterValue(fastLFO->rate)), 600, 140, 20, BLUE);
        DrawText(TextFormat("Ultra Rate: %.2f", getParameterValue(ultraLFO->rate)), 600, 190, 20, YELLOW);
        DrawText(TextFormat("Env Level: %.2f", getParameterValue(adsrEnv->base.output)), 600, 240, 20, PINK);

        EndDrawing();
    }

    // Cleanup
    cleanupModSystem(modList);
    CloseWindow();
    return 0;
}