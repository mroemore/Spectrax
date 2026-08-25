/*
 * instrument_harness -- boots straight to the FM instrument screen for
 * manual and scripted (Xvfb) UI testing of the dynamic mod-source
 * add/remove/rewire controls.
 *
 * The harness deliberately re-uses everything that lives in main.c:
 *   - initApplication() sets up the GUI, VoiceManager, Arranger,
 *     PatternList, loads s1.sng, creates the instrument graph and
 *     the arranger/pattern graphs.
 *   - initModSystem() registers the mod-source factory.
 *
 * We avoid PortAudio: this is a GUI-only harness, no audio stream.
 *
 * Linking trick: main.c compiles with -DSPECTRAX_HARNESS so its
 * own main() is excluded, leaving initApplication + helper symbols
 * available to the harness.
 *
 * Run from the bin/ directory (the same cwd the app expects),
 * so that resources/ and data/ resolve correctly:
 *
 *   cd bin && ../src/tools/instrument_harness/instrument_harness
 *
 * (Task 15 will add a --script mode for Xvfb-driven automation;
 * for now it is interactive only.)
 */

#include <stdlib.h>

#include "main.h"
#include "gui.h"
#include "graph_gui.h"
#include "input.h"
#include "appstate.h"
#include "sequencer.h"
#include "voice.h"
#include "modsystem.h"
#include "settings.h"

int main(void) {
	InitGUI();

	paTestData data;
	ApplicationState *appState;
	initApplication(&data, &appState, NULL);
	initModSystem();

	/* Associate a pattern with the arranger cell so the instrument
	 * screen has musical context. main.c's initApplication already
	 * loads s1.sng, which typically places at least one pattern in
	 * song[0][0]. If so, adopt it as the selected pattern. The
	 * instrument screen itself does not strictly require a selected
	 * pattern -- its graph is driven by VoiceManager, not by the
	 * pattern -- so if no pattern is present we still boot. */
	if(appState->selectedPattern < 0 && data.arranger->song[0][0] >= 0) {
		appState->selectedPattern = data.arranger->song[0][0];
	}
	appState->currentScene = SCENE_INSTRUMENT;

	while(!WindowShouldClose()) {
		updateInputState(appState->inputState);
		BeginDrawing();
		clearBg();

		/* instrument-screen input -- mirrors main.c's SCENE_INSTRUMENT
		 * branch (main.c:320-372) plus the KM_ADD / KM_REMOVE handlers
		 * from Task 13. */
		if(isKeyJustPressed(appState->inputState, KM_ADD)) {
			Instrument *inst = getSelectedInstInstrument();
			if(inst && inst->voiceType == VOICE_TYPE_FM) {
				addRuntimeEnvelope(inst);
			}
		}
		if(isKeyJustPressed(appState->inputState, KM_REMOVE)) {
			removeSelectedEnvelope();
		}
		if(isKeyHeld(appState->inputState, KM_FUNCTION)) {
			if(isKeyJustPressed(appState->inputState, KM_LEFT)) {
				selectArrangerCell(data.arranger, 0, -1, 0);
			}
			if(isKeyJustPressed(appState->inputState, KM_RIGHT)) {
				selectArrangerCell(data.arranger, 0, 1, 0);
			}
		}
		if(isKeyHeld(appState->inputState, KM_EDIT)) {
			Graph *currentGraph = getSelectedInstGraph();
			if(isKeyJustPressed(appState->inputState, KM_LEFT)) {
				currentGraph->selected->callback(currentGraph->selected->p, -0.1f);
			}
			if(isKeyJustPressed(appState->inputState, KM_RIGHT)) {
				currentGraph->selected->callback(currentGraph->selected->p, 0.1f);
			}
			if(isKeyJustPressed(appState->inputState, KM_UP)) {
				currentGraph->selected->callback(currentGraph->selected->p, 2.0f);
			}
			if(isKeyJustPressed(appState->inputState, KM_DOWN)) {
				currentGraph->selected->callback(currentGraph->selected->p, -2.0f);
			}
		} else {
			Graph *currentGraph = getSelectedInstGraph();
			if(isKeyJustPressed(appState->inputState, KM_LEFT)) {
				navigateGraph(currentGraph, KM_LEFT);
			}
			if(isKeyJustPressed(appState->inputState, KM_RIGHT)) {
				navigateGraph(currentGraph, KM_RIGHT);
			}
			if(isKeyJustPressed(appState->inputState, KM_UP)) {
				navigateGraph(currentGraph, KM_UP);
			}
			if(isKeyJustPressed(appState->inputState, KM_DOWN)) {
				navigateGraph(currentGraph, KM_DOWN);
			}
		}

		DrawGUI(appState->currentScene);
		EndDrawing();
	}

	CloseWindow();
	return 0;
}