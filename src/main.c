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
} paTestData;


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
	unsigned int i, j;
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
    
	
	
	for (i = 0; i < framesPerBuffer; i++)
	{
		float left_output = 0.0f;
		float right_output = 0.0f;
		
		//process instrument-level param changes:
		for(int j = 0; j < MAX_SEQUENCER_CHANNELS; j++){
			processModulations(data->voiceManager->instruments[j]->paramList, data->voiceManager->instruments[j]->modList, 1.0f / SAMPLE_RATE);
		}


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
					if (currentVoice->note[0] != OFF)
					{
						float freq = noteFrequencies[currentVoice->note[0]][currentVoice->note[1]];
						setParameterBaseValue(currentVoice->frequency, freq);
						setParameterValue(currentVoice->frequency, freq);
						phase_increment = freq / SAMPLE_RATE;
					}

					float left_value = 0.0f, right_value = 0.0f;

					if(currentVoice->type == VOICE_TYPE_FM) {
						left_value = sineFmAlgo(currentVoice->source.operators, getParameterValue(currentVoice->frequency), getParameterValueAsInt(currentVoice->instrumentRef->selectedAlgorithm));
						right_value = left_value; // Modify for stereo oscillators if needed
					} else if (currentVoice->type == VOICE_TYPE_BLEP) {
						// left_value = blep_saw(currentVoice->left_phase, phase_increment);
						// left_value *= 0.0005;
						left_value = noblep_sine(currentVoice->leftPhase);
						left_value *= 0.5;
						right_value = left_value; // Modify for stereo oscillators if needed
					} else if (currentVoice->type == VOICE_TYPE_SAMPLE) {
						left_value = getSampleValue(currentVoice->source.sample, &currentVoice->samplePosition, phase_increment, SAMPLE_RATE, 0);
						right_value = left_value; // Mono playback
					}

					// left_value = fold(left_value, 8.0f, 1.0f);
					// right_value = left_value; // Mono playback
					if(data->arranger->playing){
						// float vol = getParameterValue(currentVoice->volume);
						// if(j == 0){
						// 	printf("VOLUME: %f\n", vol);
						// }
						left_output += left_value * getParameterValue(currentVoice->volume);
						right_output += right_value * getParameterValue(currentVoice->volume);

						currentVoice->leftPhase = fmodf(currentVoice->leftPhase + phase_increment, 1.0f);
						currentVoice->rightPhase = fmodf(currentVoice->rightPhase + phase_increment, 1.0f);

						currentVoice->samplesElapsed++;
					} else {
						left_output = 0;
						right_output = 0;
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
	InputState* input = createInputState(INPUT_TYPE_KEYBOARD);
	InitGUI();
    initModSystem();
	Settings* settings = createSettings();
	loadColourSchemeTxt("colourscheme2.txt", getColorSchemeAsPointerArray(), 9);

	ApplicationState* appState = createApplicationState();
	
	data.samplePool = createSamplePool();
	loadSamplesfromDirectory("resources/samples/", data.samplePool);
	data.modList = createModList();
	data.arranger = createArranger(settings);
	data.patternList = createPatternList();
	data.voiceManager = createVoiceManager(settings, data.samplePool);

	//int loadstate = loadSequencerState("s1.sng", data.arranger, data.patternList);
	//printf("arranger/pattern load result: %i\n", loadstate);
	data.sequencer = createSequencer(data.arranger);

	if (!data.samples[0].data)
	{
		fprintf(stderr, "Failed to load sample\n");
		return 1;
	}

	TransportGui* tsGui = createTransportGui(&data.arranger->playing, data.arranger, 10, 10);
	add_drawable(&tsGui->base, GLOBAL);
	InputsGui* inputsGui = createInputsGui(appState->inputState, SCREEN_W - 22 * KEY_MAPPING_COUNT, SCREEN_H - 30);
	// add_drawable(&inputsGui->base, GLOBAL);
	// data.og = createOscilloscopeGui(0, 400, 800, 75);
	// add_drawable(&data.og->base, GLOBAL);


	// int graphCount = MAX_SEQUENCER_CHANNELS*MAX_VOICES_PER_CHANNEL;
	// GraphGui* gs[MAX_SEQUENCER_CHANNELS][settings->defaultVoiceCount];

	// int gpad = 2;	
	// int xoff = 10;
	// int gmaxw = (int)800/(xoff+gpad);
	// int yoff = 0;
	// int gheight = 12;
	// for (int i = 0; i < MAX_SEQUENCER_CHANNELS; i++)
	// {
	// 	for(int j = 0; j < settings->defaultVoiceCount; j++){
	// 		char* name = (char*)malloc(10 * sizeof(char));
	// 		snprintf(name, 10, "C%i,V:%i ",i+1, j+1);
	// 		gs[i][j] = createGraphGui(&data.voiceManager->voicePools[i][j]->volume->currentValue, name, 0.0f, 1.0f, i * 26, j*13, gheight, MAX_GRAPH_HISTORY);
	// 		add_drawable(&gs[i][j]->base, SCENE_PATTERN);
	// 	}
	// }
	
	data.active_sequencer_index = 0;
	data.sequence_index = 0;
	data.samples_per_beat = (int)(SAMPLE_RATE * 60)/(120*2);
	data.samples_elapsed = 0;
	
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

	SequencerGui *seqGui = createSequencerGui(data.sequencer, data.patternList, &appState->selectedPattern, &appState->selectedStep, 10, 10);
	add_drawable(&seqGui->base, SCENE_PATTERN);
	
	ArrangerGui *arrGui = createArrangerGui(data.arranger, data.patternList, 10, 10);
	add_drawable(&arrGui->base, SCENE_ARRANGER);

	SongMinimapGui *songMinimapGui = createSongMinimapGui(data.arranger, appState->selectedArrangerCell, 400, 10);
	add_drawable(&songMinimapGui->base, SCENE_PATTERN);

	ContainerGroup* instMod = createInstrumentModulationGui(data.voiceManager->instruments[1], 10, 10, SCREEN_W/2 - 20, 100, SCENE_INSTRUMENT);

	// EnvelopeGui *envGui = createEnvelopeGui(data.voices[0].envelope[0], 10, 10, 300, 100);
	// add_drawable(&envGui->base, SCENE_INSTRUMENT);
	// for(int i = 0; i < 16; i++){
	// 	printf("(%i, %i, %i, %i)\n", data.arranger->song[0][i], data.arranger->song[1][i], data.arranger->song[2][i], data.arranger->song[3][i]);
	// }

	// ContainerGroup* instControls = createContainerGroup();
	// InputContainer* envelopeControls1 = createInputContainer();
	// InputContainer* envelopeControls2 = createInputContainer();
	// InputContainer* envelopeControls3 = createInputContainer();
	// ButtonGui* attackGui1 = createButtonGui(100, 100, 30,30, "A1", data.voices[0].envelope[0]->stages[0].duration, modifyParameterBaseValue);
	// ButtonGui* sustainGui1 = createButtonGui(132, 100, 30,30, "S1", data.voices[0].envelope[0]->stages[1].duration, modifyParameterBaseValue);
	// ButtonGui* decayGui1 = createButtonGui(164, 100, 30,30, "D1", data.voices[0].envelope[0]->stages[1].duration, modifyParameterBaseValue);
	// ButtonGui* attackGui2 = createButtonGui(100, 132, 30,30, "A2", data.voices[0].envelope[0]->stages[0].duration, modifyParameterBaseValue);
	// ButtonGui* sustainGui2 = createButtonGui(132, 132, 30,30, "S2", data.voices[0].envelope[0]->stages[1].duration, modifyParameterBaseValue);
	// ButtonGui* decayGui2 = createButtonGui(164, 132, 30,30, "D2", data.voices[0].envelope[0]->stages[1].duration, modifyParameterBaseValue);
	// ButtonGui* attackGui3 = createButtonGui(100, 164, 30,30, "A3", data.voices[0].envelope[0]->stages[0].duration, modifyParameterBaseValue);
	// ButtonGui* sustainGui3 = createButtonGui(132, 164, 30,30, "S3", data.voices[0].envelope[0]->stages[1].duration, modifyParameterBaseValue);
	// ButtonGui* decayGui3 = createButtonGui(164, 164, 30,30, "D3", data.voices[0].envelope[0]->stages[1].duration, modifyParameterBaseValue);
	// add_drawable(&attackGui1->base, SCENE_INSTRUMENT);
	// add_drawable(&decayGui1->base, SCENE_INSTRUMENT);
	// add_drawable(&sustainGui1->base, SCENE_INSTRUMENT);
	// add_drawable(&attackGui2->base, SCENE_INSTRUMENT);
	// add_drawable(&decayGui2->base, SCENE_INSTRUMENT);
	// add_drawable(&sustainGui2->base, SCENE_INSTRUMENT);
	// add_drawable(&attackGui3->base, SCENE_INSTRUMENT);
	// add_drawable(&decayGui3->base, SCENE_INSTRUMENT);
	// add_drawable(&sustainGui3->base, SCENE_INSTRUMENT);
	// addButtonToContainer(attackGui1, envelopeControls1,0,0);
	// addButtonToContainer(sustainGui1, envelopeControls1,0,1);
	// addButtonToContainer(decayGui1, envelopeControls1, 0,2);
	// addButtonToContainer(attackGui2, envelopeControls2, 0,0);
	// addButtonToContainer(sustainGui2, envelopeControls2, 0,1);
	// addButtonToContainer(decayGui2, envelopeControls2, 0,2);
	// addButtonToContainer(attackGui3, envelopeControls3, 0,0);
	// addButtonToContainer(sustainGui3, envelopeControls3, 0,1);
	// addButtonToContainer(decayGui3, envelopeControls3, 0, 2);
	// addContainerToGroup(instControls, envelopeControls1, 0, 0);
	// addContainerToGroup(instControls, envelopeControls2, 1, 0);
	// addContainerToGroup(instControls, envelopeControls3, 2, 0);
	//ContainerGroup* modMappingGroup = createModMappingGroup(data.voices[0].paramList, &data.voices[0].envelope[0]->base, SCENE_INSTRUMENT, 300, 10);
	//removeButtonFromContainer(attackGui2, envelopeControls2, SCENE_INSTRUMENT);

	
	while (!WindowShouldClose())
	{
		updateInputState(appState->inputState);
		// for (int i = 0; i < MAX_SEQUENCER_CHANNELS; i++){
		// 	for (int j = 0; j < MAX_VOICES_PER_CHANNEL; j++){
		// 		updateGraphGui(gs[i][j]);
		// 	}
		// }
		BeginDrawing();
		clearBg();
		
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
					if(isKeyJustPressed(appState->inputState, KM_FUNCTION)){
						editCurrentNote(data.patternList, appState->selectedPattern, appState->selectedStep, (int[]){OFF,0}); //NOTE OFF
					}
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
				if(isKeyHeld(appState->inputState, KM_EDIT)){
					if(isKeyJustPressed(appState->inputState, KM_LEFT)){
						ButtonGui* btnGui = (ButtonGui*)getSelectedInput(instMod);
						btnGui->applyCallback(btnGui, -0.01f);	
					}
					if(isKeyJustPressed(appState->inputState, KM_RIGHT)){
						ButtonGui* btnGui = (ButtonGui*)getSelectedInput(instMod);
						btnGui->applyCallback(btnGui, 0.01f);
					}
					if(isKeyJustPressed(appState->inputState, KM_UP)){
						ButtonGui* btnGui = (ButtonGui*)getSelectedInput(instMod);
						btnGui->applyCallback(btnGui, 0.10f);
					}
					if(isKeyJustPressed(appState->inputState, KM_DOWN)){
						ButtonGui* btnGui = (ButtonGui*)getSelectedInput(instMod);
						btnGui->applyCallback(btnGui, -0.10f);
					}
				} else{
					if(isKeyJustPressed(appState->inputState, KM_LEFT)){
					containerGroupNavigate(instMod, 0, -1);
					}
					if(isKeyJustPressed(appState->inputState, KM_RIGHT)){
						containerGroupNavigate(instMod, 0, 1);
					}
					if(isKeyJustPressed(appState->inputState, KM_UP)){
						containerGroupNavigate(instMod, -1, 0);
					}
					if(isKeyJustPressed(appState->inputState, KM_DOWN)){
						containerGroupNavigate(instMod, 1, 0);
					}
				}
				
			break;
			
		}
		
		
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