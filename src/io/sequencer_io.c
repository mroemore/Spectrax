#include "sequencer_io.h"

SequencerFileResult saveSequencerState(const char *filename, Arranger *arranger, PatternList *patterns) {
	FILE *file = fopen(filename, "wb");
	if(!file) return SEQ_ERROR_OPEN;

	// Write file header (V2 — includes per-channel preset slots + chip labels).
	if(!writeChunkHeader(file, SEQ_MAGIC_HEADER_V2)) {
		fclose(file);
		return SEQ_ERROR_WRITE;
	}

	// Write patterns section. patterns is allowed to be NULL (e.g. the
	// arranger-only save path used for instrument-chip edits); in that
	// case we still write the PATT chunk with pattern_count=0 so the
	// file remains a valid SEQ2 stream.
	if(!writeChunkHeader(file, PATTERN_SECTION)) {
		fclose(file);
		return SEQ_ERROR_WRITE;
	}

	int pat_count = (patterns != NULL) ? patterns->pattern_count : 0;
	fwrite(&pat_count, sizeof(int), 1, file);
	for(int i = 0; i < pat_count; i++) {
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

	// V2: chip labels (instrument chips) — colour index + 8-char label
	// per channel. Always present in V2; absent in V1.
	if(!writeChunkHeader(file, CHIP_LABELS_SECTION)) {
		fclose(file);
		return SEQ_ERROR_WRITE;
	}
	fwrite(arranger->labelColourIdx, sizeof(int), MAX_SEQUENCER_CHANNELS, file);
	// Labels are stored as fixed-length 9-byte fields (8 chars + NUL).
	// Writing one contiguous block is cleaner than MAX_SEQUENCER_CHANNELS
	// fwrite calls and keeps the on-disk layout compact.
	fwrite(arranger->label, sizeof(char), MAX_SEQUENCER_CHANNELS * 9, file);

	fclose(file);
	return SEQ_OK;
}

SequencerFileResult loadSequencerState(const char *filename, Arranger *arranger, PatternList *patterns) {
	FILE *file = fopen(filename, "rb");
	if(!file) return SEQ_ERROR_OPEN;

	// Accept either SEQ2 (V2, has channelSlots + LABL) or SEQ1 (legacy V1, no channelSlots).
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

	// Read patterns. patterns may be NULL (instrument-chip load path) —
	// in that case we read the count into a local so the file stream
	// stays in sync but the caller doesn't get any Pattern data. A
	// non-zero count with a NULL destination is a format error.
	int pat_count = 0;
	if(fread(&pat_count, sizeof(int), 1, file) != 1) {
		fclose(file);
		printf("error reading pattern count\n");
		return SEQ_ERROR_READ;
	}

	if(pat_count > MAX_PATTERNS) {
		fclose(file);
		return SEQ_ERROR_FORMAT;
	}

	if(patterns == NULL) {
		if(pat_count != 0) {
			// Can't store the patterns but the file claims to have some.
			fclose(file);
			return SEQ_ERROR_FORMAT;
		}
		// PATT chunk was empty; nothing more to read for it.
	} else {
		patterns->pattern_count = pat_count;
		for(int i = 0; i < pat_count; i++) {
			Pattern *p = &patterns->patterns[i];
			if(fread(&p->pattern_size, sizeof(int), 1, file) != 1 ||
			   fread(p->notes, sizeof(int), MAX_SEQUENCE_LENGTH * NOTE_INFO_SIZE, file) != MAX_SEQUENCE_LENGTH * NOTE_INFO_SIZE) {
				fclose(file);
				return SEQ_ERROR_READ;
				printf("error reading pattern data\n");
			}
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

	if(is_v2) {
		/* LABL chunk (chip labels) is OPTIONAL — older V2 files predate
		 * it (e.g. the shipped bin/s1.sng). Peek the magic; if it's
		 * LABL read the body, otherwise default + rewind so any future
		 * trailing content still reads from the right offset. */
		long lablPos = ftell(file);
		char labl_magic[4];
		if(fread(labl_magic, 1, 4, file) == 4 && memcmp(labl_magic, CHIP_LABELS_SECTION, 4) == 0) {
			if(fread(arranger->labelColourIdx, sizeof(int), MAX_SEQUENCER_CHANNELS, file) != MAX_SEQUENCER_CHANNELS) {
				fclose(file);
				printf("error reading labelColourIdx (V2)\n");
				return SEQ_ERROR_READ;
			}
			// Defensive clamp: chip palette has 8 entries, so any out-of-range
			// value on disk would index past the LUT and read garbage.
			for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
				if(arranger->labelColourIdx[i] < 0 || arranger->labelColourIdx[i] > 7) {
					arranger->labelColourIdx[i] = 0;
				}
			}
			if(fread(arranger->label, sizeof(char), MAX_SEQUENCER_CHANNELS * 9, file) != MAX_SEQUENCER_CHANNELS * 9) {
				fclose(file);
				printf("error reading label strings (V2)\n");
				return SEQ_ERROR_READ;
			}
		} else {
			fseek(file, lablPos, SEEK_SET);
			for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
				arranger->labelColourIdx[i] = 0;
				memset(arranger->label[i], 0, 9);
			}
		}
	} else {
		// V1 / legacy file: no LABL chunk. Default every channel's chip
		// colour to 0 and clear every 8-char label so the UI sees a
		// blank-slate instrument-chip row.
		for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
			arranger->labelColourIdx[i] = 0;
			memset(arranger->label[i], 0, 9);
		}
	}

	fclose(file);
	return SEQ_OK;
}
