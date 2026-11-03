#include "sequencer_io.h"

SequencerFileResult saveSequencerState(const char *filename, Arranger *arranger, PatternList *patterns, const PatternTrackSet *tracks) {
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
	/* Songs always persist in a non-playing state: playback position is
	 * owned by the live session, and restoring playing=1 from the file
	 * made the arranger highlight the last row while createSequencer had
	 * already seeded the sequencer at row 0 — audio played the first
	 * pattern while the UI showed the last. The field stays in the layout
	 * for backward compatibility; it is written 0 and clamped on read. */
	int playing = 0;
	fwrite(&playing, sizeof(int), 1, file);
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

	/* PTRK: per-channel pattern-sequence tracks. Optional — skipped when
	 * the caller passes NULL (arranger-only saves + legacy tests). */
	if(tracks != NULL) {
		if(!writeChunkHeader(file, PATTERN_TRACKS_SECTION)) {
			fclose(file);
			return SEQ_ERROR_WRITE;
		}
		if(fwrite(tracks->track, sizeof(tracks->track), 1, file) != 1) {
			fclose(file);
			return SEQ_ERROR_WRITE;
		}
	}

	/* VELO: the per-step velocities. Optional and trailing, like LABL and
	 * PTRK, so a file without it loads unchanged.
	 *
	 * Only written when a step actually has a velocity: a song that never
	 * uses velocity stays byte-identical to what it was before velocity
	 * existed, which is what keeps the old files and the fixtures
	 * unchanged.
	 *
	 * The body is the pattern count, then one int per step per pattern --
	 * the same shape the PATT chunk uses for the notes, so the two line
	 * up step for step. */
	if(patterns != NULL) {
		bool anyVelocity = false;
		for(int i = 0; i < patterns->pattern_count && !anyVelocity; i++) {
			for(int j = 0; j < MAX_SEQUENCE_LENGTH; j++) {
				if(patterns->patterns[i].velocity[j] != 0) {
					anyVelocity = true;
					break;
				}
			}
		}
		if(anyVelocity) {
			if(!writeChunkHeader(file, VELOCITY_SECTION)) {
				fclose(file);
				return SEQ_ERROR_WRITE;
			}
			int count = patterns->pattern_count;
			if(fwrite(&count, sizeof(int), 1, file) != 1) {
				fclose(file);
				return SEQ_ERROR_WRITE;
			}
			for(int i = 0; i < patterns->pattern_count; i++) {
				if(fwrite(patterns->patterns[i].velocity, sizeof(int), MAX_SEQUENCE_LENGTH, file)
				   != MAX_SEQUENCE_LENGTH) {
					fclose(file);
					return SEQ_ERROR_WRITE;
				}
			}
		}
	}

	fclose(file);
	return SEQ_OK;
}

SequencerFileResult loadSequencerState(const char *filename, Arranger *arranger, PatternList *patterns, PatternTrackSet *tracks) {
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
			/* The velocities start unset, for every pattern the file
			 * holds. The VELO chunk (if the file has one) sets them
			 * afterwards; a file without one leaves every step unset, so
			 * it plays as it did before velocity existed.
			 *
			 * Without this the destination's previous velocities would
			 * survive the load, and a step the file says nothing about
			 * would play at the last session's velocity -- which changes
			 * both its level and where its sample starts. */
			for(int v = 0; v < MAX_SEQUENCE_LENGTH; v++) {
				p->velocity[v] = 0;
			}
			/* pattern_size is attacker-controlled and every consumer treats
			 * it as a step count into fixed-size arrays (Pattern.notes is
			 * MAX_SEQUENCE_LENGTH rows, and gui_pattern.c's stepData[] /
			 * stepNodes[] are too). An out-of-range value is a corrupt file,
			 * not a longer pattern. Same class as the WAV subchunk2Size bound
			 * (TESTING.md I16). */
			if(p->pattern_size < 0 || p->pattern_size > MAX_SEQUENCE_LENGTH) {
				fclose(file);
				return SEQ_ERROR_FORMAT;
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
	/* enabledChannels, selected_x and selected_y are all file-controlled and
	 * all index fixed-size arrays: createSequencer loops
	 * `for(i = 0; i < enabledChannels; i++) sequencer->playhead_index[i] = 0`
	 * over an array of MAX_SEQUENCER_CHANNELS, and
	 * getPatternIDfromArranger reads song[selected_x][selected_y]. A file
	 * claiming more channels than exist drove an out-of-bounds WRITE; a file
	 * claiming a wild cursor drove an out-of-bounds READ. Found by
	 * fuzz/fuzz_seqfile.c, which drives createSequencer + startPlaying +
	 * advanceSequencerStep on the loaded data (TESTING.md I32).
	 *
	 * These are cursors and counts, not bulk data, so clamping is the right
	 * response rather than rejecting the file: a project saved by a future
	 * build with more channels should still open, just not claim rows this
	 * build cannot address. The cursor clamps matter for files already in the
	 * wild; the channel count cannot be represented safely if negative. */
	if(arranger->enabledChannels < 0) {
		arranger->enabledChannels = 0;
	}
	if(arranger->enabledChannels > MAX_SEQUENCER_CHANNELS) {
		arranger->enabledChannels = MAX_SEQUENCER_CHANNELS;
	}
	if(arranger->selected_x < 0) {
		arranger->selected_x = 0;
	}
	if(arranger->selected_x >= MAX_SEQUENCER_CHANNELS) {
		arranger->selected_x = MAX_SEQUENCER_CHANNELS - 1;
	}
	if(arranger->selected_y < 0) {
		arranger->selected_y = 0;
	}
	if(arranger->selected_y >= MAX_SONG_LENGTH) {
		arranger->selected_y = MAX_SONG_LENGTH - 1;
	}
	/* playhead_indices[] are song-row cursors read straight from the file with
	 * no range check, and incrementSequencer does
	 * `nextRowIndex = playhead_indices[i] + 1; if(song[i][nextRowIndex] > -1)` —
	 * so an out-of-range cursor indexes past song[][] and the loaded value
	 * then becomes sequencer->pattern_index[i], i.e. the file steers playback.
	 * Found by fuzz/fuzz_seqfile.c (I32). -1 is the documented "no playhead"
	 * value that createArranger initialises to. */
	for(int i = 0; i < MAX_SEQUENCER_CHANNELS; i++) {
		if(arranger->playhead_indices[i] < -1) {
			arranger->playhead_indices[i] = -1;
		}
		if(arranger->playhead_indices[i] >= MAX_SONG_LENGTH) {
			arranger->playhead_indices[i] = MAX_SONG_LENGTH - 1;
		}
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
	/* Load non-playing (policy): the saved playing flag is ignored so a
	 * song never auto-plays at boot. Playback resumes from selected_y via
	 * startPlaying when the user presses play. */
	arranger->playing = 0;
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
		/* PTRK (pattern tracks) is OPTIONAL — older V2 files predate it.
		 * Peek the magic; on match read the body, else rewind so any
		 * future trailing chunk still reads from the right offset.
		 * A NULL destination skips the body (stream stays in sync). */
		long ptrkPos = ftell(file);
		char ptrk_magic[4];
		if(fread(ptrk_magic, 1, 4, file) == 4 && memcmp(ptrk_magic, PATTERN_TRACKS_SECTION, 4) == 0) {
			long ptrkBytes = (long)(sizeof(PatternTrackData) * MAX_SEQUENCER_CHANNELS * PATTERN_TRACKS);
			if(tracks != NULL) {
				if(fread(tracks->track, sizeof(tracks->track), 1, file) != 1) {
					fclose(file);
					printf("error reading pattern tracks (PTRK)\n");
					return SEQ_ERROR_READ;
				}
			} else {
				fseek(file, ptrkBytes, SEEK_CUR);
			}
		} else {
			fseek(file, ptrkPos, SEEK_SET);
		}
		/* VELO (the per-step velocities) is OPTIONAL — files written
		 * before velocity existed have no such chunk, and every step then
		 * reads as unset (which plays at full). Peek the magic; on a
		 * match read the body, else rewind so any future trailing chunk
		 * still reads from the right offset.
		 *
		 * The chunk is read after the patterns, so the pattern count is
		 * known: the body is the count, then one int per step per
		 * pattern. */
		long veloPos = ftell(file);
		char velo_magic[4];
		if(fread(velo_magic, 1, 4, file) == 4 && memcmp(velo_magic, VELOCITY_SECTION, 4) == 0) {
			int count = 0;
			if(fread(&count, sizeof(int), 1, file) != 1) {
				fclose(file);
				printf("error reading velocity count (VELO)\n");
				return SEQ_ERROR_READ;
			}
			if(count < 0 || count > MAX_PATTERNS) {
				fclose(file);
				return SEQ_ERROR_FORMAT;
			}
			for(int i = 0; i < count; i++) {
				int velocities[MAX_SEQUENCE_LENGTH];
				if(fread(velocities, sizeof(int), MAX_SEQUENCE_LENGTH, file) != MAX_SEQUENCE_LENGTH) {
					fclose(file);
					printf("error reading velocities (VELO)\n");
					return SEQ_ERROR_READ;
				}
				/* A pattern that was not written (the file holds more
				 * patterns than the destination has) is skipped rather
				 * than written past the end. */
				if(patterns == NULL || i >= patterns->pattern_count) {
					continue;
				}
				for(int j = 0; j < MAX_SEQUENCE_LENGTH; j++) {
					/* An out-of-range velocity in the file reads as
					 * unset, not as a value the engine would act on. */
					if(velocities[j] < 0 || velocities[j] > NOTE_VELOCITY_FULL) {
						velocities[j] = 0;
					}
					patterns->patterns[i].velocity[j] = velocities[j];
				}
			}
		} else {
			fseek(file, veloPos, SEEK_SET);
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
