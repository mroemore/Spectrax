#include <stdio.h>
#include <math.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "portaudio.h"
#include "raylib.h"
#include "settings.h"
#include "appstate.h"
#include "input.h"
#include "oscillator.h"
#include "modsystem.h"
#include "voice.h"
#include "sample.h"
#include "gui.h"
#include "io.h"
#include "io/preset_io.h"
#include "io/config_io.h"
#include "paths.h"
#include "sequencer.h"
#include "notes.h"
#include "distortion.h"
#include "graph_gui.h"
#include "dataviz.h"
#include "vizfx.h"
#include "main.h"

/* This routine will be called by the PortAudio engine when audio is needed.
** It may called at interrupt level on some machines so don't do anything
** that could mess up the system like calling malloc() or free().
*/
static int patestCallback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags, void *userData) {
	paTestData *data = (paTestData *)userData;
	float *out = (float *)outputBuffer;
	unsigned int i = 0;
	unsigned int j = 0;
	/* Hold the audio lock for the whole buffer: every deref of the
	 * instrument lists / voice pool below is shared with the GUI thread,
	 * whose type-swap / preset-apply / resize paths free them under the
	 * same lock. Without it the `rebuilding` flag leaves a check-then-use
	 * window where the audio thread touches a freed voice (type-cycle
	 * segfault). Non-contended in practice — the GUI only takes it during
	 * a reconfiguration, which skips a buffer at worst. */
	pthread_mutex_lock(&g_audioLock);
	float max_output = 0.0f;
	(void)inputBuffer;
	clock_t start, end;
	double cpu_time_used;
	start = clock();
	int stepSamples = data->arranger->tempoSettings.swingStep ? data->arranger->tempoSettings.samplesPerOddStep : data->arranger->tempoSettings.samplesPerEvenStep;
	if(data->arranger->tempoSettings.samplesElapsed >= stepSamples) {
		data->arranger->tempoSettings.samplesElapsed = 0;
		if(data->arranger->playing) {
			incrementSequencer(data->sequencer, data->patternList, data->arranger);
			for(int sc = 0; sc < data->arranger->enabledChannels; sc++) {
				if(data->sequencer->running[sc]) {
					int *note = getCurrentStep(data->patternList, data->sequencer->pattern_index[sc], data->sequencer->playhead_index[sc]);
					if(note[0] != OFF) {
						Voice *voice = getFreeVoice(data->voiceManager, sc);
						triggerVoice(voice, note);
					}
				}
			}
		}
	}

	// process instrument-level param changes:
	for(int j = 0; j < MAX_SEQUENCER_CHANNELS; j++) {
		Instrument *chInst = data->voiceManager->instruments[j];
		/* Task 8: skip channels whose lists are being rebuilt by the GUI
		 * thread (applyInstrumentPreset / voice rebuild). Reading them
		 * here mid-rebuild is a use-after-free -> segfault. */
		if(chInst->rebuilding) {
			continue;
		}
		processModulations(chInst->paramList, chInst->modList, 1.0f / framesPerBuffer);
	}
	// process song-level param changes:
	processModulations(data->globalParameters, data->modList, 1.0f / framesPerBuffer);

	for(i = 0; i < framesPerBuffer; i++) {
		float left_output = 0.0f;
		float right_output = 0.0f;

		for(j = 0; j < data->arranger->enabledChannels; j++) {
			/* Task 8: skip channels whose voice pool is mid-rebuild (the
			 * Voice structs are freed + re-malloc'd by rebuildVoicesForInstrument
			 * on preset change). Iterating voicePools[j][v] here would
			 * deref a freed Voice. */
			if(data->voiceManager->instruments[j]->rebuilding) {
				continue;
			}
			for(int v = 0; v < data->voiceManager->voiceCount[j]; v++) {
				Voice *currentVoice = data->voiceManager->voicePools[j][v];
				if(currentVoice->active) {
					processModulations(currentVoice->paramList, currentVoice->modList, 1.0f / SAMPLE_RATE);
					// Handle envelope
					if(!currentVoice->envelope[0]->isTriggered) {
						setParameterValue(currentVoice->volume, 1.0f);
						setParameterBaseValue(currentVoice->volume, 1.0f);
						currentVoice->active = 0;
						// printf("%i not triggered...", j);
						if(currentVoice->type == VOICE_TYPE_SAMPLE) {
							currentVoice->vd.sampler.samplePosition = 0.0f;
						}
					}

					float phase_increment = 0.0f;
					float freq = 0.0f;
					if(currentVoice->note[0] != OFF) {
						freq = noteFrequencies[currentVoice->note[0]][currentVoice->note[1]];
						setParameterBaseValue(currentVoice->frequency, freq);
						setParameterValue(currentVoice->frequency, freq);
						phase_increment = freq / SAMPLE_RATE;
					}

					OutVal currentSample = generateVoice(data->voiceManager, currentVoice, phase_increment, freq);

					/* Voices always mix while active so that stopping the
					 * song lets the current notes ring out through their
					 * AD envelope (which ends at 0 amplitude) instead of
					 * hard-zeroing the output mid-cycle. The old
					 * `playing ? mix : zero` branch snapped every active
					 * voice's sample to 0 the instant `playing` flipped --
					 * a step discontinuity, i.e. the click/pop heard on
					 * start and stop. */
					left_output += currentSample.L * getParameterValue(currentVoice->volume);
					right_output += currentSample.R * getParameterValue(currentVoice->volume);
					currentVoice->leftPhase = fmodf(currentVoice->leftPhase + phase_increment, 1.0f);
					currentVoice->rightPhase = fmodf(currentVoice->rightPhase + phase_increment, 1.0f);

					currentVoice->samplesElapsed++;
					if(!data->arranger->playing) {
						currentSample.L = 0;
						currentSample.R = 0;
						left_output = 0.0f;
						right_output = 0.0f;
					}
				} else {
					//	data->voices[j].mod[0].result = 0;
				}
			}
		}

		// Track the maximum output value
		if(fabsf(left_output) > max_output)
			max_output = fabsf(left_output);
		if(fabsf(right_output) > max_output)
			max_output = fabsf(right_output);

		*out++ = left_output;
		*out++ = right_output;

		pushFrameToFFT(&data->spectrogram.fft, left_output);

		pushBufferScrollerFrame(&data->bufferScroller, left_output);
		pushMixRingSample(&data->mixRing, left_output);

		data->arranger->tempoSettings.samplesElapsed++;
	}

	processFFTData(&data->spectrogram.fft);

	// Normalize the entire buffer to avoid clipping
	//  if (max_output > MAX_VOLUME)
	//  {
	//  	float normalization_factor = MAX_VOLUME / max_output;
	//  	out = (float *)outputBuffer;
	//  	for (i = 0; i < framesPerBuffer * 2; i++)
	//  	{
	//  		*out++ *= normalization_factor;
	//  	}
	//  }
	end = clock();
	cpu_time_used = (((double)(end - start)) / CLOCKS_PER_SEC) * 1000.0f;
	pushTimeGraphMeasurement(&data->timeGraph, cpu_time_used);
	pthread_mutex_unlock(&g_audioLock);
	return 0;
}

#ifndef SPECTRAX_HARNESS
int main(int argc, char **argv) {
	PaStream *stream;
	PaError err;
	paTestData data;
	ApplicationState *appState;

	/* Task 2 (XDG split): resolve config + data dirs up front, chdir into
	 * the data dir before anything opens files, and stash configDir so
	 * the exit path can rebuild absolute cfg.json / clr.json paths. */
	char cfgDir[1024];
	char dataDir[1024];
	resolveConfigDir(argc, argv, cfgDir, sizeof(cfgDir));
	resolveDataDir(argc, argv, dataDir, sizeof(dataDir));
	snprintf(data.configDir, sizeof(data.configDir), "%s", cfgDir);

	/* Load cfg from the config dir, populate the theme file name. */
	Settings settings;
	char cfgPath[1088];
	snprintf(cfgPath, sizeof(cfgPath), "%s/cfg.json", cfgDir);
	createSettings(&settings);
	loadSettingsJson(cfgPath, &settings, settings.themeFile, sizeof(settings.themeFile));
	if(settings.themeFile[0] == '\0') {
		strncpy(settings.themeFile, "clr.json", sizeof(settings.themeFile) - 1);
		settings.themeFile[sizeof(settings.themeFile) - 1] = '\0';
	}

	/* Seed FontConfig with hardcoded defaults so it stays usable even if
	 * loadThemeJson can't find the theme file. */
	FontConfig fontCfg;
	strncpy(fontCfg.path, "resources/fonts/console.ttf", sizeof(fontCfg.path) - 1);
	fontCfg.path[sizeof(fontCfg.path) - 1] = '\0';
	fontCfg.size = 9;
	fontCfg.spacing = 1;
	setFontConfig(&fontCfg);

	/* Only claim "theme loaded" if the file actually exists, so InitGUI
	 * can fall back to initDefaultColourScheme on a fresh config dir. */
	char clrPath[1088];
	snprintf(clrPath, sizeof(clrPath), "%s/%s", cfgDir, settings.themeFile);
	FILE *th = fopen(clrPath, "rb");
	if(th) {
		fclose(th);
		/* Always seed every key with its default BEFORE the theme load:
		 * cs is a zero-initialised global, so any colour key the theme
		 * file lacks (e.g. keys added after the file was first written)
		 * would otherwise stay transparent black -- and the next save
		 * would persist the zeros (#00000000) permanently. */
		initDefaultColourScheme(getColourScheme());
		loadThemeJson(clrPath, getColourScheme(), getFontConfig());
		markThemeLoaded();
	}

	/* Now chdir to the data dir for samples / songs / presets. */
	if(!chdirToDataDir(dataDir)) {
		fprintf(stderr, "spectrax: cannot use data dir '%s'\n", dataDir);
		return 1;
	}

	// loading screen
	InitGUI();
	Texture2D loadingImage = LoadTexture("resources/images/spectrax_splash5_fix_2x.png");

	BeginDrawing();
	ClearBackground(BLACK);
	float scale = loadingImage.height > SCREEN_H ? (float)SCREEN_H / loadingImage.height : .5;
	int xOffset = (int)((loadingImage.width * scale - SCREEN_W) / 2);
	DrawTextureEx(loadingImage, (Vector2){ xOffset, 0 }, 0.0f, scale, WHITE);
	EndDrawing();

	initApplication(&data, &appState, NULL);
	initModSystem();

	err = Pa_Initialize();
	if(err != paNoError)
		goto error;

	err = Pa_OpenDefaultStream(&stream,
	                           0,
	                           2,
	                           paFloat32,
	                           SAMPLE_RATE,
	                           256,
	                           patestCallback,
	                           &data);
	if(err != paNoError)
		goto error;

	err = Pa_StartStream(stream);
	if(err != paNoError)
		goto error;
	SetTraceLogLevel(LOG_WARNING);
	RenderTexture2D gfx = createPresentTarget();
	while(!WindowShouldClose()) {
		updateInputState(appState->inputState);
		/* Task 6: window-scale keybinds. Ctrl+=/- steps the window scale
		 * in whole multiples of the base (1x->2x->3x...); adding Shift
		 * steps in 0.25 increments. Global (all scenes). */
		if(isKeyHeld(appState->inputState, KM_CTRL)) {
			bool scaleShift = isKeyHeld(appState->inputState, KM_SHIFT);
			if(isKeyJustPressed(appState->inputState, KM_EQUAL)) {
				setWindowScale(scaleShift ? getWindowScale() + 0.25f : nextWholeScale(getWindowScale()));
			}
			if(isKeyJustPressed(appState->inputState, KM_MINUS)) {
				setWindowScale(scaleShift ? getWindowScale() - 0.25f : prevWholeScale(getWindowScale()));
			}
		}
		BeginTextureMode(gfx);
		clearBg();
		updateSpectrogramData(&data.spectrogram);
		updateTimeGraphData(&data.timeGraph);
		updateBufferScrollerData(&data.bufferScroller);
		/* Task 3: playhead-follow. While the song is playing, push the
		 * arranger window so the active playhead row stays in view.
		 * Without this, scrolling the window away while playing would
		 * hide the row that's currently audible. We only follow the
		 * channel-0 playhead; multi-channel playheads can be handled in
		 * a later task. scrollArrangerWindowTo both clamps and re-targets
		 * each cell's row field — writing visibleStart alone leaves the
		 * graph rendering the wrong rows (defect 3 regression). */
		if(data.arranger->playing) {
			int playheadRow = data.arranger->playhead_indices[0];
			int ws = data.arranger->visibleStart;
			if(playheadRow < ws) {
				scrollArrangerWindowTo(data.arranger, playheadRow);
			} else if(playheadRow >= ws + ARRANGER_WINDOW_ROWS) {
				scrollArrangerWindowTo(data.arranger, playheadRow - ARRANGER_WINDOW_ROWS + 1);
			}
		}
		// printf("checking inputs...\n");
		// Global Navigation Controls
		if(isKeyJustPressed(appState->inputState, KM_START)) {
			data.arranger->playing ? stopPlaying(data.arranger) : startPlaying(data.sequencer, data.patternList, data.arranger, appState->currentScene);
		}
		if(isKeyHeld(appState->inputState, KM_SELECT)) {
			if(isKeyJustPressed(appState->inputState, KM_LEFT)) {
				decrementScene(appState);
			}
			if(isKeyJustPressed(appState->inputState, KM_RIGHT)) {
				incrementScene(appState);
			}
		}
		// Scene specific controls
		switch(appState->currentScene) {
			case SCENE_ARRANGER: {
				/* Task 5: chip-expanded collapse from the "global"
				 * modifiers. KM_SELECT and KM_START both collapse a
				 * chip when JUST pressed AND KM_EDIT is NOT held —
				 * otherwise the SELECT+EDIT "add blank" combo below
				 * would unintentionally collapse a chip the user is
				 * actively editing. This sits BEFORE the modifier-held
				 * branches so it takes precedence on the press frame. */
				int chForCollapse = getSelectedChipChannel();
				if(chForCollapse >= 0 && isChipExpanded(chForCollapse)
						&& !isKeyHeld(appState->inputState, KM_EDIT)) {
					if(isKeyJustPressed(appState->inputState, KM_SELECT)) {
						handleExpandedChipInput(chForCollapse, KM_SELECT, false);
					} else if(isKeyJustPressed(appState->inputState, KM_START)) {
						handleExpandedChipInput(chForCollapse, KM_START, false);
					}
				}
				if(isKeyHeld(appState->inputState, KM_SELECT)) {
					if(isKeyJustPressed(appState->inputState, KM_EDIT)) {
						addBlankIfEmpty(data.patternList, data.arranger, appState->selectedArrangerCell[0], appState->selectedArrangerCell[1]);
						int pid = data.arranger->song[appState->selectedArrangerCell[0]][appState->selectedArrangerCell[1]];
						if(pid != -1) {
							setSelectedPattern(appState, &pid);
						}
					}
				} else if(isKeyHeld(appState->inputState, KM_FUNCTION)) {
					if(isKeyJustPressed(appState->inputState, KM_EDIT)) {
						data.arranger->song[appState->selectedArrangerCell[0]][appState->selectedArrangerCell[1]] = -1;
					}
				} else if(isKeyHeld(appState->inputState, KM_EDIT)) {
					/* Task 4: chip EDIT+arrows behaviour. When the selected
					 * node is a chip, EDIT+LEFT/RIGHT jumps to the instrument
					 * page for that channel (selectedArrangerCell[0] drives
					 * igui->selectedInstrument, which DrawGUI uses to pick the
					 * right instrumentScreenGraph). EDIT+UP toggles the chip's
					 * expanded flag. Anything else (DOWN, or no chip selected)
					 * falls through to the dial-edit dispatch below. */
					int chipChannel = getSelectedChipChannel();
					if(chipChannel >= 0) {
						/* Task 5: when the chip is expanded, route every
						 * arrow + the bare EDIT just-pressed to the
						 * expanded-chip dispatcher. handleExpandedChipInput
						 * itself decides swatch / cursor / char cycle / collapse
						 * based on whether KM_EDIT is held + the arrow dir.
						 * Bare EDIT (no arrow) collapses. This sits BEFORE
						 * the Task 4 chip-jump branch so a chip that's
						 * expanded stays in expanded mode — EDIT+LEFT must
						 * move the cursor, not jump to the instrument page. */
						if(isChipExpanded(chipChannel)) {
							if(isKeyJustPressed(appState->inputState, KM_LEFT)) {
								handleExpandedChipInput(chipChannel, KM_LEFT, true);
							} else if(isKeyJustPressed(appState->inputState, KM_RIGHT)) {
								handleExpandedChipInput(chipChannel, KM_RIGHT, true);
							} else if(isKeyJustPressed(appState->inputState, KM_UP)) {
								handleExpandedChipInput(chipChannel, KM_UP, true);
							} else if(isKeyJustPressed(appState->inputState, KM_DOWN)) {
								handleExpandedChipInput(chipChannel, KM_DOWN, true);
							}
							/* Bare EDIT just-pressed (no arrow) collapses. */
							if(isKeyJustPressed(appState->inputState, KM_EDIT)
									&& !isKeyJustPressed(appState->inputState, KM_LEFT)
									&& !isKeyJustPressed(appState->inputState, KM_RIGHT)
									&& !isKeyJustPressed(appState->inputState, KM_UP)
									&& !isKeyJustPressed(appState->inputState, KM_DOWN)) {
								handleExpandedChipInput(chipChannel, KM_EDIT, false);
							}
						} else {
							if(isKeyJustPressed(appState->inputState, KM_LEFT) || isKeyJustPressed(appState->inputState, KM_RIGHT)) {
								appState->selectedArrangerCell[0] = chipChannel;
								appState->selectedArrangerCell[1] = data.arranger->selected_y;
								appState->currentScene = SCENE_INSTRUMENT;
							}
							if(isKeyJustPressed(appState->inputState, KM_UP)) {
								expandChip(chipChannel, !isChipExpanded(chipChannel));
							}
						}
					} else {
						if(isKeyJustPressed(appState->inputState, KM_LEFT)) {
							arrangerGraphControlInput(KM_LEFT);
						}
						if(isKeyJustPressed(appState->inputState, KM_RIGHT)) {
							arrangerGraphControlInput(KM_RIGHT);
						}
						if(isKeyJustPressed(appState->inputState, KM_UP)) {
							arrangerGraphControlInput(KM_UP);
						}
						if(isKeyJustPressed(appState->inputState, KM_DOWN)) {
							arrangerGraphControlInput(KM_DOWN);
						}
					}
				} else {
					/* Task 5: bare-arrow navigation. If a chip is expanded,
					 * LEFT/RIGHT move the swatch focus + write the colour
					 * live; UP/DOWN still navigate the arranger graph
					 * (chips don't own vertical swatch navigation — there
					 * are only 8 colours laid out horizontally).
					 *
					 * Important (review feedback, task 5): when a chip IS
					 * expanded, the chip owns horizontal nav entirely, so
					 * skip the navigateArrangerGraph LEFT/RIGHT calls —
					 * otherwise the same frame would both cycle the swatch
					 * AND jump to the adjacent chip. UP/DOWN are still
					 * delegated to the graph because handleExpandedChipInput
					 * is a no-op for bare vertical arrows. */
					int bareCh = getSelectedChipChannel();
					bool chipExpanded = (bareCh >= 0) && isChipExpanded(bareCh);
					if(chipExpanded) {
						if(isKeyJustPressed(appState->inputState, KM_LEFT)) {
							handleExpandedChipInput(bareCh, KM_LEFT, false);
						} else if(isKeyJustPressed(appState->inputState, KM_RIGHT)) {
							handleExpandedChipInput(bareCh, KM_RIGHT, false);
						}
					} else {
						if(isKeyJustPressed(appState->inputState, KM_LEFT)) {
							navigateArrangerGraph(KM_LEFT);
						}
						if(isKeyJustPressed(appState->inputState, KM_RIGHT)) {
							navigateArrangerGraph(KM_RIGHT);
						}
					}
					if(isKeyJustPressed(appState->inputState, KM_UP)) {
						navigateArrangerGraph(KM_UP);
					}
					if(isKeyJustPressed(appState->inputState, KM_DOWN)) {
						navigateArrangerGraph(KM_DOWN);
					}
				}
				}
				break;
			case SCENE_PATTERN:
				if(isKeyHeld(appState->inputState, KM_FUNCTION)) {
					if(isKeyJustPressed(appState->inputState, KM_EDIT)) {
						editCurrentNote(data.patternList, appState->selectedPattern, appState->selectedStep, (int[]){ OFF, 0 }); // NOTE OFF
					}
					if(isKeyJustPressed(appState->inputState, KM_LEFT)) {
						selectArrangerCell(data.arranger, 1, -1, 0);
						appState->selectedPattern = data.arranger->song[appState->selectedArrangerCell[0]][appState->selectedArrangerCell[1]];
					}
					if(isKeyJustPressed(appState->inputState, KM_RIGHT)) {
						selectArrangerCell(data.arranger, 1, 1, 0);
						appState->selectedPattern = data.arranger->song[appState->selectedArrangerCell[0]][appState->selectedArrangerCell[1]];
					}
					if(isKeyJustPressed(appState->inputState, KM_UP)) {
						selectArrangerCell(data.arranger, 1, 0, -1);
						appState->selectedPattern = data.arranger->song[appState->selectedArrangerCell[0]][appState->selectedArrangerCell[1]];
					}
					if(isKeyJustPressed(appState->inputState, KM_DOWN)) {
						selectArrangerCell(data.arranger, 1, 0, 1);
						appState->selectedPattern = data.arranger->song[appState->selectedArrangerCell[0]][appState->selectedArrangerCell[1]];
					}
				} else if(isKeyHeld(appState->inputState, KM_EDIT)) {
					if(isKeyJustPressed(appState->inputState, KM_LEFT)) {
						editCurrentNoteRelative(data.patternList, appState->selectedPattern, appState->selectedStep, (int[]){ -1, 0 });
					} else if(isKeyJustPressed(appState->inputState, KM_RIGHT)) {
						editCurrentNoteRelative(data.patternList, appState->selectedPattern, appState->selectedStep, (int[]){ 1, 0 });
					} else if(isKeyJustPressed(appState->inputState, KM_UP)) {
						editCurrentNoteRelative(data.patternList, appState->selectedPattern, appState->selectedStep, (int[]){ 0, 1 });
					} else if(isKeyJustPressed(appState->inputState, KM_DOWN)) {
						editCurrentNoteRelative(data.patternList, appState->selectedPattern, appState->selectedStep, (int[]){ 0, -1 });
					} else {
						if(currentNoteIsBlank(data.patternList, appState->selectedPattern, appState->selectedStep)) {
							printf("blank! setting: %i %i", appState->lastUsedNote[0], appState->lastUsedNote[1]);
							setCurrentNote(data.patternList, appState->selectedPattern, appState->selectedStep, appState->lastUsedNote);
						} else {
							int *currentStep = getStep(data.patternList, appState->selectedPattern, appState->selectedStep);
							appState->lastUsedNote[0] = currentStep[0];
							appState->lastUsedNote[1] = currentStep[1];
							printf("Grabbing step: %i %i\n", appState->lastUsedNote[0], appState->lastUsedNote[1]);
						}
					}
				} else {
					if(isKeyJustPressed(appState->inputState, KM_LEFT)) {
						navigatePatternGraph(KM_LEFT);
					}
					if(isKeyJustPressed(appState->inputState, KM_RIGHT)) {
						navigatePatternGraph(KM_RIGHT);
					}
					if(isKeyJustPressed(appState->inputState, KM_UP)) {
						navigatePatternGraph(KM_UP);
					}
					if(isKeyJustPressed(appState->inputState, KM_DOWN)) {
						navigatePatternGraph(KM_DOWN);
					}
				}
				break;
			case SCENE_INSTRUMENT:
				if(isKeyJustPressed(appState->inputState, KM_ADD)) {
					Instrument *inst = getSelectedInstInstrument();
					if(inst->voiceType == VOICE_TYPE_FM) {
						addRuntimeSource(inst);
					}
				}
				if(isKeyJustPressed(appState->inputState, KM_REMOVE)) {
					/* Task 4: the mod-wrap now has a header row at index 0,
					 * so removeSelectedSource's `idx - 1` mapping is correct.
					 * removeSelectedEnvelope (the no-header walker) is gone. */
					removeSelectedSource();
				}
				if(isKeyHeld(appState->inputState, KM_FUNCTION)) {
					if(isKeyJustPressed(appState->inputState, KM_LEFT)) {
						selectArrangerCell(data.arranger, 0, -1, 0);
						// updateInstrumentGui(instrumentGui);
					}
					if(isKeyJustPressed(appState->inputState, KM_RIGHT)) {
						selectArrangerCell(data.arranger, 0, 1, 0);
						// updateInstrumentGui(instrumentGui);
					}
				}
				/* Task 4: handlePresetUiInput always runs first. It drives
				 * the preset name node (KM_EDIT enter/exit, KM_START commit,
				 * arrow cycle/cursor), the load-list modal, and the SAVE/LOAD
				 * buttons. If it consumed the event (return true), break so
				 * navigation / dial-adjustment don't double-fire on the same
				 * frame. */
				if(handlePresetUiInput(appState->inputState, getSelectedInstInstrument())) {
					break;
				}
				Graph *currentGraph = getSelectedInstGraph();

				if(isKeyHeld(appState->inputState, KM_EDIT)) {
					/* Task 3: only dispatch the value callback when the
					 * selected node is a real dial. Pressing EDIT+arrow
					 * on the preset name node (or an action button)
					 * must be a no-op, not a crash. */
					if(isSelectedDialNode(currentGraph)) {
						bool firedCallback = false;
						if(isKeyJustPressed(appState->inputState, KM_LEFT)) {
							currentGraph->selected->callback(currentGraph->selected->p, -0.1f);
							firedCallback = true;
						}
						if(isKeyJustPressed(appState->inputState, KM_RIGHT)) {
							currentGraph->selected->callback(currentGraph->selected->p, 0.1f);
							firedCallback = true;
						}
						if(isKeyJustPressed(appState->inputState, KM_UP)) {
							currentGraph->selected->callback(currentGraph->selected->p, 2.0f);
							firedCallback = true;
						}
						if(isKeyJustPressed(appState->inputState, KM_DOWN)) {
							currentGraph->selected->callback(currentGraph->selected->p, -2.0f);
							firedCallback = true;
						}
						/* Task 6: any dial callback that just fired moves
						 * the live state away from the last loaded/saved
						 * snapshot, so flip dirty. The flag only sticks if
						 * loaded.valid is true (see isInstrumentDirty) —
						 * editing a brand-new, never-loaded instrument
						 * stays "not dirty" because there's no baseline. */
						if(firedCallback) {
							Instrument *editInst = getSelectedInstInstrument();
							if(editInst) {
								editInst->loaded.dirty = true;
							}
						}
					}
				} else {
					if(isKeyJustPressed(appState->inputState, KM_LEFT)) {
						navigateGraphRefined(currentGraph, KM_LEFT);
					}
					if(isKeyJustPressed(appState->inputState, KM_RIGHT)) {
						navigateGraphRefined(currentGraph, KM_RIGHT);
					}
					if(isKeyJustPressed(appState->inputState, KM_UP)) {
						navigateGraphRefined(currentGraph, KM_UP);
					}
					if(isKeyJustPressed(appState->inputState, KM_DOWN)) {
						navigateGraphRefined(currentGraph, KM_DOWN);
					}
				}
				break;
			default:
				break;
		}
		if(isKeyHeld(appState->inputState, KM_MOD_EXTRA)) {
			if(isKeyJustPressed(appState->inputState, KM_START)) {
				toggleSpectrogram(&data.spectrogram);
				toggleSpectrogram(&data.timeGraph);
			}
			if(isKeyJustPressed(appState->inputState, KM_RIGHT)) {
				incWindowFunc(&data.spectrogram.fft, true);
			}
			if(isKeyJustPressed(appState->inputState, KM_LEFT)) {
				incWindowFunc(&data.spectrogram.fft, false);
			}
			if(isKeyJustPressed(appState->inputState, KM_SELECT)) {
				printArrGraph();
			}
		}
		// printf("drawing GUI... %i\n", appState->currentScene);
		DrawGUI(appState->currentScene);

		drawSpectrogram(&data.spectrogram);
		drawTimeGraph(&data.timeGraph);
		DrawFPS(SCREEN_W - 80, 5);
		EndTextureMode();
		presentFrame(gfx);
	}
	/* GL resource cleanup must run BEFORE CloseWindow destroys the GL
	 * context — UnloadTexture after CloseWindow segfaults in
	 * rlUnloadTexture (rlgl derefs the dead context). */
	UnloadRenderTexture(gfx);
	freeBufferScroller(&data.bufferScroller);
	CloseWindow();
	int saveResult = saveSequencerState("s1.sng", data.arranger, data.patternList);
	if(data.settings) {
		/* Reconstruct absolute cfg.json + clr.json paths under the
		 * config dir resolved at startup (cwd is now the data dir). */
		char cfgPath[1088];
		char clrPath[1088];
		snprintf(cfgPath, sizeof(cfgPath), "%s/cfg.json", data.configDir);
		snprintf(clrPath, sizeof(clrPath), "%s/%s", data.configDir, data.settings->themeFile);
		saveSettingsJson(cfgPath, data.settings, data.settings->themeFile);
		saveThemeJson(clrPath, getColourScheme(), getFontConfig());
	}
	printf("song save attempt result: %i", saveResult);
	err = Pa_StopStream(stream);
	if(err != paNoError)
		goto error;
	err = Pa_CloseStream(stream);
	if(err != paNoError)
		goto error;
	Pa_Terminate();

	freeSamplePool(data.samplePool);
	cleanupModSystem(data.modList);

	printf("The end! :).\n");
	return err;
error:
	Pa_Terminate();
	/// fclose(data.log_file);

	freeSamplePool(data.samplePool);

	cleanupModSystem(data.modList);

	fprintf(stderr, "An error occurred while using the portaudio stream\n");
	fprintf(stderr, "Error number: %d\n", err);
	fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(err));
	return err;
}
#endif /* SPECTRAX_HARNESS */

void initApplication(paTestData *data, ApplicationState **appState, InstrumentGui **instrumentGui) {
	/* Task 2 (XDG split): settings come from the config dir, not cwd.
	 * Fall back to "." so the harness's zero-initialised configDir still
	 * resolves to a sensible directory when run without main(). */
	if(data->configDir[0] == '\0') {
		strncpy(data->configDir, ".", sizeof(data->configDir) - 1);
		data->configDir[sizeof(data->configDir) - 1] = '\0';
	}
	char cfgPath[1088];
	snprintf(cfgPath, sizeof(cfgPath), "%s/cfg.json", data->configDir);
	/* Settings are loaded here so data->settings reflects cfg.json — the
	 * exit path saves this struct, so without the load a fresh default-
	 * filled Settings would overwrite the user's config on every quit.
	 * main() separately loads cfg.json for the theme name before InitGUI. */
	Settings *settings = malloc(sizeof(Settings));
	createSettings(settings);
	loadSettingsJson(cfgPath, settings, settings->themeFile, sizeof(settings->themeFile));
	if(settings->themeFile[0] == '\0') {
		strncpy(settings->themeFile, "clr.json", sizeof(settings->themeFile) - 1);
		settings->themeFile[sizeof(settings->themeFile) - 1] = '\0';
	}
	data->settings = settings;
	initSpectrogram(&data->spectrogram, 4096, 256, 5, 1.0);
	initTimeGraph(&data->timeGraph, 1024, 0, 640, 1024, 128);
	initBufferScroller(&data->bufferScroller);
	initMixRing(&data->mixRing);
	data->globalParameters = createParamList();
	*appState = createApplicationState();
	if(!*appState) {
		printf("AppState creation failed.\n");
		return;
	}

	data->samplePool = createSamplePool();
	if(!data->samplePool) {
		printf("samplePool creation failed.\n");
		return;
	}
	loadSamplesfromDirectory("resources/samples/", data->samplePool);
	data->modList = createModList();
	if(!data->modList) {
		printf("modList creation failed.\n");
		return;
	}

	data->patternList = createPatternList(*appState);
	if(!data->patternList) {
		printf("patternList creation failed.\n");
		return;
	}
	data->wavetablePool = createWavetablePool();
	if(!data->wavetablePool) {
		printf("wavetablePool creation failed.\n");
		return;
	}

	initPresetBank(&data->presetBank);
	loadPresetsFromDirectory("data/instrument_presets/", &data->presetBank);
	fillEmptyBankSlots(&data->presetBank);
	printf("\n\nPRESETS LOADED: %i\n\n", data->presetBank.presetCount);
	data->voiceManager = createVoiceManager(settings, data->samplePool, data->wavetablePool, &data->presetBank);
	if(!data->voiceManager) {
		printf("voiceManager creation failed.\n");
		return;
	}
	data->arranger = createArranger(settings, data->voiceManager, *appState, data->globalParameters);
	if(!data->arranger) {
		printf("arranger creation failed.\n");
		return;
	}
	int loadstate = loadSequencerState("s1.sng", data->arranger, data->patternList);
	printf("arranger/pattern load result: %i\n", loadstate);
	/* loadSequencerState restores arranger->selected_x/selected_y but
	 * never touches appState->selectedArrangerCell — that copy is only
	 * written by selectArrangerCell (the arrow-key path). Without this
	 * sync the visual cursor (drawn from arranger->selected_x/y) and the
	 * instrument screen's selected channel (read from
	 * selectedArrangerCell[0]) disagree until the user presses an arrow:
	 * the cursor shows one cell while the instrument screen edits a
	 * different channel. */
	(*appState)->selectedArrangerCell[0] = data->arranger->selected_x;
	(*appState)->selectedArrangerCell[1] = data->arranger->selected_y;
	/* Re-apply the per-channel preset the song file asked for, if the bank still
	 * holds a patch at that slot index. SEQ1 (legacy) files store channelSlots
	 * all-zero, which fall through the guard and preserve the channel-0 default
	 * patch that createVoiceManager installed. rebuildVoicesForInstrument is
	 * required because applyInstrumentPreset frees and rebuilds the instrument's
	 * paramList / envelope / operator / modSettings arrays. */
	if(data->voiceManager) {
		VoiceManager *vm = data->voiceManager;
		PresetBank *pb = &data->presetBank;
		for(int ch = 0; ch < MAX_SEQUENCER_CHANNELS; ch++) {
			int slot = data->arranger->channelSlots[ch];
			if(slot >= 0 && slot < pb->presetCount) {
				applyInstrumentPreset(vm->instruments[ch], pb->patches[slot]);
				rebuildVoicesForInstrument(vm, vm->instruments[ch]);
			}
		}
	}
	/* Seed selectedPattern from the arranger's restored selection so Shift+Right
	 * reaches the pattern screen immediately at startup. loadSequencerState
	 * restores arranger->selected_x/selected_y, but selectedPattern itself is
	 * only ever set when the user navigates an arranger cell, so it is -1
	 * here. If the selected cell holds no pattern, leave selectedPattern at -1
	 * and let incrementScene keep blocking (no empty pattern view). */
	if((*appState)->selectedPattern < 0) {
		int selX = data->arranger->selected_x;
		int selY = data->arranger->selected_y;
		if(selX >= 0 && selX < MAX_SEQUENCER_CHANNELS && selY >= 0 && selY < MAX_SONG_LENGTH &&
		   data->arranger->song[selX][selY] >= 0) {
			setSelectedPattern(*appState, &data->arranger->song[selX][selY]);
		}
	}
	data->sequencer = createSequencer(data->arranger);
	if(!data->sequencer) {
		printf("sequencer creation failed.\n");
		return;
	}

	data->active_sequencer_index = 0;
	data->sequence_index = 0;
	data->samples_per_beat = (int)(PA_SR * 60) / (120 * 4);
	data->samples_elapsed = 0;

	printf("bpm yo: %i", data->samples_per_beat);

	createPatternGraph(data->sequencer, data->patternList, &(*appState)->selectedPattern, &(*appState)->selectedStep);
	setPatternBufferScroller(&data->bufferScroller);
	setArrangerMixRing(&data->mixRing);

	SongMinimapGui *songMinimapGui = createSongMinimapGui(data->arranger, (*appState)->selectedArrangerCell, 400, 10);
	setSongMinimapGui(songMinimapGui);

	createArrangerGraph(data->arranger, data->patternList);
	createInstrumentGui(data->voiceManager, &(*appState)->selectedArrangerCell[0], SCENE_INSTRUMENT);
	printf("synthesis init complete.\n");
}
