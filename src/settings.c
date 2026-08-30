#include "settings.h"
#include <stdlib.h>
#include <stdio.h>

void createSettings(Settings *s) {
	if(!s) {
		return;
	}
	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
		s->voiceTypes[i] = i % 3;
	}
	s->voiceTypes[0] = 4;
	s->defaultSequenceLength = 16;
	s->enabledChannels = 8;
	s->defaultVoiceCount = 1;
	s->defaultBPM = 120;
	s->themeFile[0] = '\0';
}
