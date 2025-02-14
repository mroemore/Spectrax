#include <stdio.h>
#include <math.h>
#include <stdlib.h>
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
#include "sequencer.h"
#include "notes.h"
#include "distortion.h"

#define NUM_SECONDS (28)
#define NUM_NOTES 12
#define NOTE_DURATION .5 // Duration of each note in seconds
#define TEMPO_DEFAULT 120
#define MAX_VOLUME 0.8f	  // Maximum volume to avoid clipping
#define SAMPLE_COUNT 4
#define SAMPLE_FILE "resources/samples/Bass1.wav" // WAV file other file: VEC1Bass001A.wav
#define SAMPLE_FILE_2 "resources/samples/VEC2 Bassdrums Clubby 002.wav"
#define SAMPLE_FILE_3 "resources/samples/VEC2 Snares 001.wav"
#define SAMPLE_FILE_4 "resources/samples/VEC2 Cymbals HH Closed 01.wav"
#define MAX_INSTRUMENTS 4

typedef struct
{
	int sequence_index;
	int samples_per_beat;
	int samples_elapsed;
	Sample samples[SAMPLE_COUNT];
	int active_sequencer_index;
	Arranger* arranger;
	PatternList* patternList;
	Sequencer* sequencer;
	ModList* modList;
	VoiceManager* voiceManager;
	SamplePool* samplePool;
	WavetablePool* wavetablePool;
} paTestData;

void initApplication(paTestData* data, ApplicationState** appState, InstrumentGui** instrumentGui);

/* This routine will be called by the PortAudio engine when audio is needed.
** It may called at interrupt level on some machines so don't do anything
** that could mess up the system like calling malloc() or free().
*/
static int patestCallback(const void *inputBuffer, void *outputBuffer,
						  unsigned long framesPerBuffer,
						  const PaStreamCallbackTimeInfo *timeInfo,
						  PaStreamCallbackFlags statusFlags,
						  void *userData)
{
	paTestData *data = (paTestData *)userData;
	float *out = (float *)outputBuffer;
	unsigned int i = 0;
	unsigned int j = 0;
	float max_output = 0.0f;
	(void)inputBuffer;
	
	if (data->samples_elapsed >= data->samples_per_beat)
	{
		//printf("checking... \n");
		data->samples_elapsed = 0;
		if(data->arranger->playing){
			incrementSequencer(data->sequencer, data->patternList, data->arranger);
		}
		for(int sc = 0; sc < data->arranger->enabledChannels; sc++){
			if(data->sequencer->running[sc]){
				int *note = getCurrentStep(data->patternList, data->sequencer->pattern_index[sc], data->sequencer->playhead_index[sc]);
				if(note[0] != OFF){
					Voice* voice = getFreeVoice(data->voiceManager, sc);
					triggerVoice(voice, note);
				}
			}
		}
	}

	//process instrument-level param changes:
	for(int j = 0; j < MAX_SEQUENCER_CHANNELS; j++){
		processModulations(data->voiceManager->instruments[j]->paramList, data->voiceManager->instruments[j]->modList, 1.0f / framesPerBuffer);
	}
	
	
	for (i = 0; i < framesPerBuffer; i++)
	{
		float left_output = 0.0f;
		float right_output = 0.0f;


		for (j = 0; j < data->arranger->enabledChannels; j++)
		{
			for (int v = 0; v < data->voiceManager->voiceCount[j]; v++)
			{	
				Voice* currentVoice  = data->voiceManager->voicePools[j][v];
				if (currentVoice->active)
				{
					processModulations(currentVoice->paramList, currentVoice->modList, 1.0f / SAMPLE_RATE);
					// Handle envelope	
					if(!currentVoice->envelope[0]->isTriggered){
						setParameterValue(currentVoice->volume, 1.0f);
						setParameterBaseValue(currentVoice->volume, 1.0f);
						currentVoice->active = 0;
						//printf("%i not triggered...", j);
						if (currentVoice->type == VOICE_TYPE_SAMPLE){
							currentVoice->samplePosition = 0.0f;
						}
					}

					float phase_increment = 0.0f;
					float freq = 0.0f;
					if (currentVoice->note[0] != OFF)
					{
						freq = noteFrequencies[currentVoice->note[0]][currentVoice->note[1]];
						setParameterBaseValue(currentVoice->frequency, freq);
						setParameterValue(currentVoice->frequency, freq);
						phase_increment = freq / SAMPLE_RATE;
					}

					OutVal currentSample = generateVoice(data->voiceManager, currentVoice, phase_increment, freq);

					if(data->arranger->playing){
						// float vol = getParameterValue(currentVoice->volume);
						// if(j == 0){
						// 	printf("VOLUME: %f\n", vol);
						// }
						left_output += currentSample.L * getParameterValue(currentVoice->volume);
						right_output += currentSample.R * getParameterValue(currentVoice->volume);

						currentVoice->leftPhase = fmodf(currentVoice->leftPhase + phase_increment, 1.0f);
						currentVoice->rightPhase = fmodf(currentVoice->rightPhase + phase_increment, 1.0f);

						currentVoice->samplesElapsed++;
					} else {
						currentSample.L = 0;
						currentSample.R = 0;
					}
				} else {
				//	data->voices[j].mod[0].result = 0;			
				}
			}
		}

		// Track the maximum output value
		if (fabsf(left_output) > max_output)
			max_output = fabsf(left_output);
		if (fabsf(right_output) > max_output)
			max_output = fabsf(right_output);

		*out++ = left_output;
		*out++ = right_output;

		data->samples_elapsed++;
	}

	//Normalize the entire buffer to avoid clipping
	// if (max_output > MAX_VOLUME)
	// {
	// 	float normalization_factor = MAX_VOLUME / max_output;
	// 	out = (float *)outputBuffer;
	// 	for (i = 0; i < framesPerBuffer * 2; i++)
	// 	{
	// 		*out++ *= normalization_factor;
	// 	}
	// }

	return 0;
}

int main(void)
{
	PaStream *stream;
	PaError err;
	paTestData data;
	ApplicationState* appState;
	InstrumentGui* instrumentGui;

	//loading screen

	InitGUI();
	Texture2D loadingImage = LoadTexture("resources/images/spectrax_splash5_fix_2x.png");
	
	BeginDrawing();
    ClearBackground(BLACK);
    //DrawTexture(loadingImage, 0, 0, WHITE);
	float scale = loadingImage.height > SCREEN_H ? (float)SCREEN_H / loadingImage.height : .5;
	int xOffset = (int)((loadingImage.width * scale - SCREEN_W) / 2);
	DrawTextureEx(loadingImage, (Vector2){xOffset,0}, 0.0f, scale, WHITE);
	EndDrawing();
	WaitTime(2.0);

	initApplication(&data, &appState, &instrumentGui);
    initModSystem();

	err = Pa_Initialize();
	if (err != paNoError)
		goto error;

	err = Pa_OpenDefaultStream(&stream,
							   0,
							   2,
							   paFloat32,
							   SAMPLE_RATE,
							   256,
							   patestCallback,
							   &data);
	if (err != paNoError)
		goto error;

	err = Pa_StartStream(stream);
	if (err != paNoError)
		goto error;

	printf("begin loop:\n");
	while (!WindowShouldClose())
	{
		updateInputState(appState->inputState);
		BeginDrawing();
		clearBg();
		printf("checking inputs...\n");
		//Global Navigation Controls
		if(isKeyJustPressed(appState->inputState, KM_START)){
			data.arranger->playing ? stopPlaying(data.arranger) : startPlaying(data.sequencer, data.patternList, data.arranger, appState->currentScene);
		}
		if(isKeyHeld(appState->inputState, KM_SELECT)){
			if(isKeyJustPressed(appState->inputState, KM_LEFT)){
				decrementScene(appState);
			}
			if(isKeyJustPressed(appState->inputState, KM_RIGHT)){
				incrementScene(appState);
			}
		}
		//Scene specific controls
		switch(appState->currentScene){
			case SCENE_ARRANGER:
				if(isKeyHeld(appState->inputState, KM_SELECT)){
					if(isKeyJustPressed(appState->inputState, KM_EDIT)){
						addBlankIfEmpty(data.patternList, data.arranger, appState->selectedArrangerCell[0], appState->selectedArrangerCell[1]);
					}
				} else if(isKeyHeld(appState->inputState, KM_FUNCTION)){
					if(isKeyJustPressed(appState->inputState, KM_EDIT)){
						data.arranger->song[appState->selectedArrangerCell[0]][appState->selectedArrangerCell[1]] = -1;
					}
				}
				if(isKeyJustPressed(appState->inputState, KM_LEFT)){
					selectArrangerCell(data.arranger, 0, -1, 0, appState->selectedArrangerCell);
					appState->selectedPattern = data.arranger->song[appState->selectedArrangerCell[0]][appState->selectedArrangerCell[1]];
				}
				if(isKeyJustPressed(appState->inputState, KM_RIGHT)){
					selectArrangerCell(data.arranger, 0, 1, 0, appState->selectedArrangerCell);
					appState->selectedPattern = data.arranger->song[appState->selectedArrangerCell[0]][appState->selectedArrangerCell[1]];
				}
				if(isKeyJustPressed(appState->inputState, KM_UP)){
					selectArrangerCell(data.arranger, 0, 0, -1, appState->selectedArrangerCell);
					appState->selectedPattern = data.arranger->song[appState->selectedArrangerCell[0]][appState->selectedArrangerCell[1]];
				}
				if(isKeyJustPressed(appState->inputState, KM_DOWN)){
					selectArrangerCell(data.arranger, 0, 0, 1, appState->selectedArrangerCell);
					appState->selectedPattern = data.arranger->song[appState->selectedArrangerCell[0]][appState->selectedArrangerCell[1]];
				}
				break;
			case SCENE_PATTERN:
				if(isKeyHeld(appState->inputState, KM_FUNCTION)){
					if(isKeyJustPressed(appState->inputState, KM_EDIT)){
						printf("combo recopgnized...\n");
						editCurrentNote(data.patternList, appState->selectedPattern, appState->selectedStep, (int[]){OFF,0}); //NOTE OFF
					}
					if(isKeyJustPressed(appState->inputState, KM_LEFT)){
						selectArrangerCell(data.arranger, 1, -1, 0, appState->selectedArrangerCell);
						appState->selectedPattern = data.arranger->song[appState->selectedArrangerCell[0]][appState->selectedArrangerCell[1]];
					}
					if(isKeyJustPressed(appState->inputState, KM_RIGHT)){
						selectArrangerCell(data.arranger, 1, 1, 0, appState->selectedArrangerCell);
						appState->selectedPattern = data.arranger->song[appState->selectedArrangerCell[0]][appState->selectedArrangerCell[1]];
					}
					if(isKeyJustPressed(appState->inputState, KM_UP)){
						selectArrangerCell(data.arranger, 1, 0, -1, appState->selectedArrangerCell);
						appState->selectedPattern = data.arranger->song[appState->selectedArrangerCell[0]][appState->selectedArrangerCell[1]];
					}
					if(isKeyJustPressed(appState->inputState, KM_DOWN)){
						selectArrangerCell(data.arranger, 1, 0, 1, appState->selectedArrangerCell);
						appState->selectedPattern = data.arranger->song[appState->selectedArrangerCell[0]][appState->selectedArrangerCell[1]];
					}
				} else if(isKeyHeld(appState->inputState, KM_EDIT)){
					if(isKeyJustPressed(appState->inputState, KM_LEFT)){
						editCurrentNoteRelative(data.patternList, appState->selectedPattern, appState->selectedStep, (int[]){-1,0});
					}
					if(isKeyJustPressed(appState->inputState, KM_RIGHT)){
						editCurrentNoteRelative(data.patternList, appState->selectedPattern, appState->selectedStep, (int[]){1,0});
					}
					if(isKeyJustPressed(appState->inputState, KM_UP)){
						editCurrentNoteRelative(data.patternList, appState->selectedPattern, appState->selectedStep, (int[]){0,1});
					}
					if(isKeyJustPressed(appState->inputState, KM_DOWN)){
						editCurrentNoteRelative(data.patternList, appState->selectedPattern, appState->selectedStep, (int[]){0,-1});
					}
				} else {
					if(isKeyJustPressed(appState->inputState, KM_LEFT)){
						appState->selectedStep = selectStep(data.patternList, appState->selectedPattern, appState->selectedStep - 1);
					}
					if(isKeyJustPressed(appState->inputState, KM_RIGHT)){
						appState->selectedStep = selectStep(data.patternList, appState->selectedPattern, appState->selectedStep + 1);
					}
					if(isKeyJustPressed(appState->inputState, KM_UP)){
						appState->selectedStep = selectStep(data.patternList, appState->selectedPattern, appState->selectedStep - 4);
					}
					if(isKeyJustPressed(appState->inputState, KM_DOWN)){
						appState->selectedStep = selectStep(data.patternList, appState->selectedPattern, appState->selectedStep + 4);
					}
				}
				
				break;
			case SCENE_INSTRUMENT:
				if(isKeyHeld(appState->inputState, KM_FUNCTION)){
					if(isKeyJustPressed(appState->inputState, KM_LEFT)){
						selectArrangerCell(data.arranger, 0, -1, 0, appState->selectedArrangerCell);	
						updateInstrumentGui(instrumentGui);
					}
					if(isKeyJustPressed(appState->inputState, KM_RIGHT)){
						selectArrangerCell(data.arranger, 0, 1, 0, appState->selectedArrangerCell);	
						updateInstrumentGui(instrumentGui);
					}
				}
				if(isKeyHeld(appState->inputState, KM_EDIT)){
					ContainerGroup* currentInstGroup = instrumentGui->instrumentControls[*instrumentGui->selectedInstrument];

					if(isKeyJustPressed(appState->inputState, KM_LEFT)){
						ButtonGui* btnGui = (ButtonGui*)getSelectedInput(currentInstGroup);
						btnGui->applyCallback(btnGui, -0.01f);	
					}
					if(isKeyJustPressed(appState->inputState, KM_RIGHT)){
						ButtonGui* btnGui = (ButtonGui*)getSelectedInput(currentInstGroup);
						btnGui->applyCallback(btnGui, 0.01f);
					}
					if(isKeyJustPressed(appState->inputState, KM_UP)){
						ButtonGui* btnGui = (ButtonGui*)getSelectedInput(currentInstGroup);
						btnGui->applyCallback(btnGui, 2.0f);
					}
					if(isKeyJustPressed(appState->inputState, KM_DOWN)){
						ButtonGui* btnGui = (ButtonGui*)getSelectedInput(currentInstGroup);
						btnGui->applyCallback(btnGui, -2.0f);
					}
				} else{
					ContainerGroup* currentInstGroup = instrumentGui->instrumentControls[*instrumentGui->selectedInstrument];
					
					if(isKeyJustPressed(appState->inputState, KM_LEFT)){
						containerGroupNavigate(currentInstGroup, 0, -1);
					}
					if(isKeyJustPressed(appState->inputState, KM_RIGHT)){
						containerGroupNavigate(currentInstGroup, 0, 1);
					}
					if(isKeyJustPressed(appState->inputState, KM_UP)){
						containerGroupNavigate(currentInstGroup, -1, 0);
					}
					if(isKeyJustPressed(appState->inputState, KM_DOWN)){
						containerGroupNavigate(currentInstGroup, 1, 0);
					}
				}
				
			break;
			
		}
		
		printf("drawing GUI...\n");
		DrawGUI(appState->currentScene);
		EndDrawing();
	}
	CloseWindow();
	CleanupGUI();
	int saveResult = saveSequencerState("s1.sng", data.arranger, data.patternList);
	saveColourScheme("CLR.dat", getColourScheme());
	printf("song save attempt result: %i", saveResult);
	err = Pa_StopStream(stream);
	if (err != paNoError)
		goto error;
	err = Pa_CloseStream(stream);
	if (err != paNoError)
		goto error;
	Pa_Terminate();

	
	freeSamplePool(data.samplePool);
	cleanupModSystem(data.modList);

	printf("The end! :).\n");
	return err;
error:
	Pa_Terminate();
	///fclose(data.log_file);
	
	freeSamplePool(data.samplePool);

	cleanupModSystem(data.modList);


	fprintf(stderr, "An error occurred while using the portaudio stream\n");
	fprintf(stderr, "Error number: %d\n", err);
	fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(err));
	return err;
}

void initApplication(paTestData* data, ApplicationState** appState, InstrumentGui** instrumentGui){
	Settings* settings = createSettings();
	loadColourSchemeTxt("colourscheme2.txt", getColorSchemeAsPointerArray(), 9);

	*appState = createApplicationState();
	if(!*appState){
		printf("AppState creation failed.\n");
		return;
	}
	
	data->samplePool = createSamplePool();
	if(!data->samplePool){
		printf("samplePool creation failed.\n");
		return;
	}
	loadSamplesfromDirectory("resources/samples/", data->samplePool);
	data->modList = createModList();
	if(!data->modList){
		printf("modList creation failed.\n");
		return;
	}
	data->arranger = createArranger(settings);
	if(!data->arranger){
		printf("arranger creation failed.\n");
		return;
	}
	data->patternList = createPatternList();
	if(!data->patternList){
		printf("patternList creation failed.\n");
		return;
	}
	data->wavetablePool = createWavetablePool();
	if(!data->wavetablePool){
		printf("wavetablePool creation failed.\n");
		return;
	}
	data->voiceManager = createVoiceManager(settings, data->samplePool, data->wavetablePool);
	if(!data->voiceManager){
		printf("voiceManager creation failed.\n");
		return;
	}
	//int loadstate = loadSequencerState("s1.sng", data.arranger, data.patternList);
	//printf("arranger/pattern load result: %i\n", loadstate);

	data->sequencer = createSequencer(data->arranger);
	if(!data->sequencer){
		printf("sequencer creation failed.\n");
		return;
	}
	TransportGui* tsGui = createTransportGui(&data->arranger->playing, data->arranger, 10, 10);
	add_drawable(&tsGui->base, GLOBAL);
	InputsGui* inputsGui = createInputsGui((*appState)->inputState, SCREEN_W - 22 * KEY_MAPPING_COUNT, SCREEN_H - 30);
	add_drawable(&inputsGui->base, GLOBAL);
	
	data->active_sequencer_index = 0;
	data->sequence_index = 0;
	data->samples_per_beat = (int)(SAMPLE_RATE * 60)/(120*2);
	data->samples_elapsed = 0;

	printf("bpm yo: %i", data->samples_per_beat);

	SequencerGui *seqGui = createSequencerGui(data->sequencer, data->patternList, &(*appState)->selectedPattern, &(*appState)->selectedStep, 10, 10);
	add_drawable(&seqGui->base, SCENE_PATTERN);
	
	ArrangerGui *arrGui = createArrangerGui(data->arranger, data->patternList);
	add_drawable(&arrGui->base, SCENE_ARRANGER);

	SongMinimapGui *songMinimapGui = createSongMinimapGui(data->arranger, (*appState)->selectedArrangerCell, 400, 10);
	add_drawable(&songMinimapGui->base, SCENE_PATTERN);

	*instrumentGui = createInstrumentGui(data->voiceManager,&(*appState)->selectedArrangerCell[0], SCENE_INSTRUMENT);
	printf("synthesis init complete.\n");
}