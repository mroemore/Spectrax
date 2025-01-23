#include "settings.h"
#include <stdlib.h>
#include <stdio.h>

Settings* createSettings(){
	Settings* settings = (Settings*)malloc(sizeof(Settings));
	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++){
		settings->voiceTypes[i] = (i % 3);
		printf("V%i :%i\n", i, settings->voiceTypes[i]);
	}
	settings->defaultSequenceLength = 16;
	settings->enabledChannels = 8;
	settings->defaultVoiceCount = 4;
}