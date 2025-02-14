#include "settings.h"
#include <stdlib.h>
#include <stdio.h>

Settings* createSettings(){
	printf("creating settings.\n");
	Settings* settings = (Settings*)malloc(sizeof(Settings));
	if(!settings){
		printf("could not allocate memory for settings.\n");
		return NULL;
	}
	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++){
		settings->voiceTypes[i] = (i % 3);
		printf("V%i :%i\n", i, settings->voiceTypes[i]);
	}
	settings->defaultSequenceLength = 16;
	settings->enabledChannels = 8;
	settings->defaultVoiceCount = 4;
}