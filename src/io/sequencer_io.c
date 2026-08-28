#include "sequencer_io.h"

SequencerFileResult saveSequencerState(const char *filename, Arranger *arranger, PatternList *patterns) {
	FILE *file = fopen(filename, "wb");
	if(!file) return SEQ_ERROR_OPEN;

	// Write file header (V2 — includes per-channel preset slots).
	if(!writeChunkHeader(file, SEQ_MAGIC_HEADER_V2)) {
		fclose(file);
		return SEQ_ERROR_WRITE;
	}

	// Write patterns section
	if(!writeChunkHeader(file, PATTERN_SECTION)) {
		fclose(file);
		return SEQ_ERROR_WRITE;
	}

	// Write pattern count and patterns
	fwrite(&patterns->pattern_count, sizeof(int), 1, file);
	for(int i = 0; i < patterns->pattern_count; i++) {
		Pattern *p = &patterns->patterns[i];
		fwrite(&p->pattern_size, sizeof(int), 1, file);
		fwrite(p->notes, sizeof(int), MAX_SEQUENCE_LENGTH * NOTE_INFO_SIZE, file);
	}

	// Write arranger section
	if(!writeChunkHeader(file, ARRANGER_SECTION)) {
		fclose(file);
		return SEQ_ERROR_WRITE;
	}

	// Write complete arranger struct
	fwrite(arranger->playhead_indices, sizeof(int), MAX_SEQUENCER_CHANNELS, file);
	fwrite(&arranger->enabledChannels, sizeof(int), 1, file);
	fwrite(&arranger->selected_x, sizeof(int), 1, file);
	fwrite(&arranger->selected_y, sizeof(int), 1, file);
	fwrite(&arranger->tempoSettings.loop, sizeof(int), 1, file);
	int bpm = getParameterValueAsInt(arranger->tempoSettings.bpm);
	fwrite(&bpm, sizeof(int), 1, file);
	fwrite(&arranger->playing, sizeof(int), 1, file);
	// fwrite(arranger->voiceTypes, sizeof(int), MAX_SEQUENCER_CHANNELS, file);
	// V2: song block written first, then per-channel preset slot index after song.
	fwrite(arranger->song, sizeof(int), MAX_SEQUENCER_CHANNELS * MAX_SONG_LENGTH, file);
	fwrite(arranger->channelSlots, sizeof(int), MAX_SEQUENCER_CHANNELS, file);

	fclose(file);
	return SEQ_OK;
}

SequencerFileResult loadSequencerState(const char *filename, Arranger *arranger, PatternList *patterns) {
	FILE *file = fopen(filename, "rb");
	if(!file) return SEQ_ERROR_OPEN;

	// Accept either SEQ2 (V2, has channelSlots) or SEQ1 (legacy V1, no channelSlots).
	char magic[4];
	if(fread(magic, 1, 4, file) != 4) {
		fclose(file);
		return SEQ_ERROR_FORMAT;
	}
	int is_v2 = (memcmp(magic, SEQ_MAGIC_HEADER_V2, 4) == 0);
	if(!is_v2 && memcmp(magic, SEQ_MAGIC_HEADER, 4) != 0) {
		fclose(file);
		return SEQ_ERROR_FORMAT;
	}

	if(!readAndVerifyChunkHeader(file, PATTERN_SECTION)) {
		fclose(file);
		return SEQ_ERROR_FORMAT;
	}

	// Read patterns
	if(fread(&patterns->pattern_count, sizeof(int), 1, file) != 1) {
		fclose(file);
		printf("error reading pattern count\n");
		return SEQ_ERROR_READ;
	}

	if(patterns->pattern_count > MAX_PATTERNS) {
		fclose(file);
		return SEQ_ERROR_FORMAT;
	}

	for(int i = 0; i < patterns->pattern_count; i++) {
		Pattern *p = &patterns->patterns[i];
		if(fread(&p->pattern_size, sizeof(int), 1, file) != 1 ||
		   fread(p->notes, sizeof(int), MAX_SEQUENCE_LENGTH * NOTE_INFO_SIZE, file) != MAX_SEQUENCE_LENGTH * NOTE_INFO_SIZE) {
			fclose(file);
			return SEQ_ERROR_READ;
			printf("error reading pattern data\n");
		}
	}

	// Read arranger section
	if(!readAndVerifyChunkHeader(file, ARRANGER_SECTION)) {
		fclose(file);
		return SEQ_ERROR_FORMAT;
	}

	if(fread(arranger->playhead_indices, sizeof(int), MAX_SEQUENCER_CHANNELS, file) != MAX_SEQUENCER_CHANNELS) {
		fclose(file);
		printf("error playhread\n");
		return SEQ_ERROR_READ;
	}
	if(fread(&arranger->enabledChannels, sizeof(int), 1, file) != 1) {
		fclose(file);
		printf("error enabledchans\n");
		return SEQ_ERROR_READ;
	}
	if(fread(&arranger->selected_x, sizeof(int), 1, file) != 1) {
		fclose(file);
		printf("error selx\n");
		return SEQ_ERROR_READ;
	}
	if(fread(&arranger->selected_y, sizeof(int), 1, file) != 1) {
		fclose(file);
		printf("error sely\n");
		return SEQ_ERROR_READ;
	}
	if(fread(&arranger->tempoSettings.loop, sizeof(int), 1, file) != 1) {
		fclose(file);
		printf("error loop\n");
		return SEQ_ERROR_READ;
	}
	int bpm;
	if(fread(&bpm, sizeof(int), 1, file) != 1) {
		fclose(file);
		printf("error bpm\n");
		return SEQ_ERROR_READ;
	}
	setParameterBaseValue(arranger->tempoSettings.bpm, bpm);

	if(fread(&arranger->playing, sizeof(int), 1, file) != 1) {
		fclose(file);
		printf("error ply\n");
		return SEQ_ERROR_READ;
	}
	// if(fread(arranger->voiceTypes, sizeof(int), MAX_SEQUENCER_CHANNELS, file) != MAX_SEQUENCER_CHANNELS) {
	// 	fclose(file);
	// 	printf("error voicetypes\n");
	// 	return SEQ_ERROR_READ;
	// }
	if(fread(arranger->song, sizeof(int), MAX_SEQUENCER_CHANNELS * MAX_SONG_LENGTH, file) != MAX_SEQUENCER_CHANNELS * MAX_SONG_LENGTH) {
		fclose(file);
		printf("error reading arranger data.\n");

		return SEQ_ERROR_READ;
	}

	if(is_v2) {
		if(fread(arranger->channelSlots, sizeof(int), MAX_SEQUENCER_CHANNELS, file) != MAX_SEQUENCER_CHANNELS) {
			fclose(file);
			printf("error reading channelSlots (V2)\n");
			return SEQ_ERROR_READ;
		}
	} else {
		// V1 / legacy file: no channelSlots field, default every slot to 0.
		for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
			arranger->channelSlots[i] = 0;
		}
	}

	fclose(file);
	return SEQ_OK;
}
