#include <string.h>
#include "io.h"

FileResult saveSettings(const char* filename, Settings* settings) {
    FILE* file = fopen(filename, "wb");
    if (!file) return FILE_ERROR_OPEN;
    
    const char magic[] = "SET1";
    fwrite(magic, 1, 4, file);
    fwrite(settings, sizeof(Settings), 1, file);
    
    fclose(file);
    return FILE_OK;
}

FileResult loadSettings(const char* filename, Settings* settings) {
    FILE* file = fopen(filename, "rb");
    if (!file) return FILE_ERROR_OPEN;
    
    char magic[4];
    if (fread(magic, 1, 4, file) != 4 || memcmp(magic, "SET1", 4) != 0) {
        fclose(file);
        return FILE_ERROR_FORMAT;
    }
    
    if (fread(settings, sizeof(Settings), 1, file) != 1) {
        fclose(file);
        return FILE_ERROR_READ;
    }
    
    fclose(file);
    return FILE_OK;
}

FileResult saveColourScheme(const char* filename, ColourScheme* colourScheme) {
    FILE* file = fopen(filename, "wb");
    if (!file) return FILE_ERROR_OPEN;
    
    const char magic[] = "CSC1";
    fwrite(magic, 1, 4, file);
    fwrite(colourScheme, sizeof(ColourScheme), 1, file);
    
    fclose(file);
    return FILE_OK;
}

FileResult loadColourScheme(const char* filename, ColourScheme* colourScheme) {
    FILE* file = fopen(filename, "rb");
    if (!file) return FILE_ERROR_OPEN;
    
    char magic[4];
    if (fread(magic, 1, 4, file) != 4 || memcmp(magic, "CSC1", 4) != 0) {
        fclose(file);
        return FILE_ERROR_FORMAT;
    }
    
    if (fread(colourScheme, sizeof(ColourScheme), 1, file) != 1) {
        fclose(file);
        return FILE_ERROR_READ;
    }
    
    fclose(file);
    return FILE_OK;
}

FileResult loadColourSchemeTxt(const char* filename, Color* colourArray[], int arraySize) {
    FILE* file = fopen(filename, "rb");
    if (!file) return FILE_ERROR_OPEN;
    
    char line[32];
    for (int i = 0; i < arraySize; i++) {
        if (!fgets(line, sizeof(line), file)) {
            fclose(file);
            return FILE_ERROR_READ;
        }
        
        int r, g, b;
        if (sscanf(line, "%d,%d,%d", &r, &g, &b) != 3 ||
            r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
            fclose(file);
            return FILE_ERROR_FORMAT;
        }
        
        colourArray[i]->r = r;
        colourArray[i]->g = g;
        colourArray[i]->b = b;
    }
    
    fclose(file);
    return FILE_OK;
}

static int writeChunkHeader(FILE* file, const char* id) {
    return fwrite(id, 1, 4, file) == 4;
}

static int readAndVerifyChunkHeader(FILE* file, const char* expected) {
    char header[4];
    if (fread(header, 1, 4, file) != 4) return 0;
    return memcmp(header, expected, 4) == 0;
}
SequencerFileResult saveSequencerState(const char* filename, Arranger* arranger, PatternList* patterns) {
    FILE* file = fopen(filename, "wb");
    if (!file) return SEQ_ERROR_OPEN;

    // Write file header
    if (!writeChunkHeader(file, MAGIC_HEADER)) {
        fclose(file);
        return SEQ_ERROR_WRITE;
    }

    // Write patterns section
    if (!writeChunkHeader(file, PATTERN_SECTION)) {
        fclose(file);
        return SEQ_ERROR_WRITE;
    }

    // Write pattern count and patterns
    fwrite(&patterns->pattern_count, sizeof(int), 1, file);
    for (int i = 0; i < patterns->pattern_count; i++) {
        Pattern* p = &patterns->patterns[i];
        fwrite(&p->pattern_size, sizeof(int), 1, file);
        fwrite(p->notes, sizeof(int), MAX_SEQUENCE_LENGTH * NOTE_INFO_SIZE, file);
    }

    // Write arranger section
    if (!writeChunkHeader(file, ARRANGER_SECTION)) {
        fclose(file);
        return SEQ_ERROR_WRITE;
    }

    // Write complete arranger struct
    fwrite(arranger->playhead_indices, sizeof(int), MAX_SEQUENCER_CHANNELS, file);
    fwrite(&arranger->enabledChannels, sizeof(int), 1, file);
    fwrite(&arranger->selected_x, sizeof(int), 1, file);
    fwrite(&arranger->selected_y, sizeof(int), 1, file);
    fwrite(&arranger->loop, sizeof(int), 1, file);
    fwrite(&arranger->beats_per_minute, sizeof(int), 1, file);
    fwrite(&arranger->playing, sizeof(int), 1, file);
    fwrite(&arranger->voiceTypes, sizeof(int), MAX_SEQUENCER_CHANNELS, file);
    fwrite(arranger->song, sizeof(int), MAX_SEQUENCER_CHANNELS * MAX_SONG_LENGTH, file);

    fclose(file);
    return SEQ_OK;
}

SequencerFileResult loadSequencerState(const char* filename, Arranger* arranger, PatternList* patterns) {
    FILE* file = fopen(filename, "rb");
    if (!file) return SEQ_ERROR_OPEN;

    if (!readAndVerifyChunkHeader(file, MAGIC_HEADER)) {
        fclose(file);
        return SEQ_ERROR_FORMAT;
    }

    if (!readAndVerifyChunkHeader(file, PATTERN_SECTION)) {
        fclose(file);
        return SEQ_ERROR_FORMAT;
    }

    // Read patterns
    if (fread(&patterns->pattern_count, sizeof(int), 1, file) != 1) {
        fclose(file);
        return SEQ_ERROR_READ;
    }

    if (patterns->pattern_count > MAX_PATTERNS) {
        fclose(file);
        return SEQ_ERROR_FORMAT;
    }

    for (int i = 0; i < patterns->pattern_count; i++) {
        Pattern* p = &patterns->patterns[i];
        if (fread(&p->pattern_size, sizeof(int), 1, file) != 1 ||
            fread(p->notes, sizeof(int), MAX_SEQUENCE_LENGTH * NOTE_INFO_SIZE, file) 
                != MAX_SEQUENCE_LENGTH * NOTE_INFO_SIZE) {
            fclose(file);
            return SEQ_ERROR_READ;
        }
    }

    // Read arranger section
    if (!readAndVerifyChunkHeader(file, ARRANGER_SECTION)) {
        fclose(file);
        return SEQ_ERROR_FORMAT;
    }

    if (fread(arranger->playhead_indices, sizeof(int), MAX_SEQUENCER_CHANNELS, file) != MAX_SEQUENCER_CHANNELS ||
        fread(&arranger->enabledChannels, sizeof(int), 1, file) != 1 ||
        fread(&arranger->selected_x, sizeof(int), 1, file) != 1 ||
        fread(&arranger->selected_y, sizeof(int), 1, file) != 1 ||
        fread(&arranger->loop, sizeof(int), 1, file) != 1 ||
        fread(&arranger->beats_per_minute, sizeof(int), 1, file) != 1 ||
        fread(&arranger->playing, sizeof(int), 1, file) != 1 ||
        fread(&arranger->voiceTypes, sizeof(int), MAX_SEQUENCER_CHANNELS, file) != 1 ||
        fread(arranger->song, sizeof(int), MAX_SEQUENCER_CHANNELS * MAX_SONG_LENGTH, file) 
            != MAX_SEQUENCER_CHANNELS * MAX_SONG_LENGTH) {
        fclose(file);
        return SEQ_ERROR_READ;
    }

    fclose(file);
    return SEQ_OK;
}