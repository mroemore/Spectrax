#ifndef SETTINGS_H
#define SETTINGS_H

#define PA_SR 44100
#define PA_BUFFER_SIZE 256
#define MAX_SEQUENCER_CHANNELS 16
#define MAX_VOICES_PER_CHANNEL 8
#define MAX_PATTERNS 255
#define MAX_SONG_LENGTH 255
#define MAX_SEQUENCE_LENGTH 24
#define NOTE_INFO_SIZE 2 // One for note index, one for octave index
#define TWOPI (2.0f * M_PI)

#define SCREEN_W 640 // 640
#define SCREEN_H 480 // 480

#ifndef INSTALL_DIR
#define INSTALL_DIR "C:/msys64/home/Krang/portaudio/build/bin/"
#endif

#define SETTINGS_PATH "settings.dat"
#define SONG_FOLDER_PATH "songs/"
#define SAMPLE_FOLDER_PATH "samples/"

typedef enum {
	GLOBAL,
	SCENE_ARRANGER,
	SCENE_PATTERN,
	SCENE_INSTRUMENT,
	SCENE_COUNT
} Scene;

typedef struct {
	int enabledChannels;
	int defaultSequenceLength;
	int voiceTypes[MAX_SEQUENCER_CHANNELS];
	int defaultVoiceCount;
	int defaultBPM;
} Settings;

Settings *createSettings();
#endif
