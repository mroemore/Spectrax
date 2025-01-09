#include "settings.h"
#include <stdlib.h>
#include <stdio.h>

Settings* createSettings(){
	Settings* settings = (Settings*)malloc(sizeof(Settings));
	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++){
		settings->voiceTypes[i] = 1 + (i % 3);
		printf("V:%i", settings->voiceTypes[i]);
	}
	settings->defaultSequenceLength = 16;
	settings->enabledChannels = 4;
	settings->defaultVoiceCount = 4;
}