#ifndef IO_H
#define IO_H

#include <stdio.h>
#include <stdlib.h>
#include "gui.h"
#include "settings.h"
#include "sequencer.h"
#include "sample.h"

#define MAGIC_HEADER "SEQ1"
#define PATTERN_SECTION "PATT"
#define ARRANGER_SECTION "ARRG"

// File operation results
typedef enum {
    FILE_OK,
    FILE_ERROR_OPEN,
    FILE_ERROR_READ,
    FILE_ERROR_WRITE,
    FILE_ERROR_FORMAT
} FileResult;

typedef enum {
    SEQ_OK,
    SEQ_ERROR_OPEN,
    SEQ_ERROR_READ,
    SEQ_ERROR_WRITE,
    SEQ_ERROR_FORMAT,
    SEQ_ERROR_MEMORY
} SequencerFileResult;


//Sample load_raw_sample(const char *filename, int sample_rate);
Sample load_wav_sample(const char *filename);
/**
 * @brief Saves a colour scheme to a binary file
 * @param filename Path to save the colour scheme file
 * @param colourScheme Pointer to ColourScheme structure to save
 * @return FileResult indicating success (FILE_OK) or specific error codes:
 *         - FILE_ERROR_OPEN if file cannot be created/opened for writing
 */
FileResult saveColourScheme(const char* filename, ColourScheme* colourScheme);
/**
 * @brief Loads a colour scheme from a binary file
 * @param filename Path to the colour scheme file to load
 * @param colourScheme Pointer to ColourScheme structure to populate
 * @return FileResult indicating success (FILE_OK) or specific error codes:
 *         - FILE_ERROR_OPEN if file cannot be opened
 *         - FILE_ERROR_FORMAT if file has invalid magic number
 *         - FILE_ERROR_READ if reading colour scheme data fails
 */
FileResult loadColourScheme(const char* filename, ColourScheme* colourScheme);
/**
 * @brief Loads a colour scheme from a text file format
 * @param filename Path to the text-based colour scheme file
 * @param colourScheme Pointer to ColourScheme structure to populate
 * @return FileResult indicating success (FILE_OK) or specific error codes:
 *         - FILE_ERROR_OPEN if file cannot be opened
 *         - FILE_ERROR_FORMAT if file format is invalid
 *         - FILE_ERROR_READ if reading colour data fails
 */
FileResult loadColourSchemeTxt(const char* filename, Color* colourArray[], int arraySize);
/**
 * @brief Loads the complete sequencer state from a binary file
 * @param filename Path to the sequencer state file to load
 * @param arranger Pointer to Arranger structure to populate with playhead, channel and song data
 * @param patterns Pointer to PatternList structure to populate with pattern data
 * @return SequencerFileResult indicating success (SEQ_OK) or specific error codes:
 *         - SEQ_ERROR_OPEN if file cannot be opened
 *         - SEQ_ERROR_FORMAT if file format or section headers are invalid
 */
SequencerFileResult saveSequencerState(const char* filename, Arranger* arranger, PatternList* patterns);
/**
 * @brief Loads the complete sequencer state from a binary file
 * @param filename Path to the sequencer state file to load
 * @param arranger Pointer to Arranger structure to populate with playhead, channel and song data
 * @param patterns Pointer to PatternList structure to populate with pattern data
 * @return SequencerFileResult indicating success (SEQ_OK) or specific error codes:
 *         - SEQ_ERROR_OPEN if file cannot be opened
 *         - SEQ_ERROR_FORMAT if file format or section headers are invalid
 */
SequencerFileResult loadSequencerState(const char* filename, Arranger* arranger, PatternList* patterns);
/**
 * @brief Saves application settings to a binary file
 * @param filename Path to save the settings file
 * @param settings Pointer to Settings structure to save
 * @return FileResult indicating success (FILE_OK) or specific error codes:
 *         - FILE_ERROR_OPEN if file cannot be created/opened for writing
 */
FileResult saveSettings(const char* filename, Settings* settings);
/**
 * @brief Loads application settings from a binary file
 * @param filename Path to the settings file to load
 * @param settings Pointer to Settings structure to populate
 * @return FileResult indicating success (FILE_OK) or specific error codes:
 *         - FILE_ERROR_OPEN if file cannot be opened
 *         - FILE_ERROR_FORMAT if file has invalid magic number
 *         - FILE_ERROR_READ if reading settings data fails
 */
FileResult loadSettings(const char* filename, Settings* settings);

#endif
