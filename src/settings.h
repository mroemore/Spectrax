#ifndef SETTINGS_H
#define SETTINGS_H

/* The engine's sample rate. Every synthesis timebase -- phase increments,
 * envelope and LFO clocks, the sequencer's step cadence -- derives from
 * this constant, so a voice's output is a pure function of its settings and
 * never of the device it plays on. The device's own rate is a boundary fact
 * (g_deviceRate): a device that cannot run at this rate cannot carry the
 * engine's output without a boundary resampler, and the stream open refuses
 * it rather than playing every note at the wrong pitch. */
#define SPECTRAX_ENGINE_RATE 48000

/* The rate the sequencer derives its step cadence from. It used to follow
 * the opened stream's rate, which made the tempo drift with the device. */
#define PA_SR SPECTRAX_ENGINE_RATE

/* The rate the opened audio device actually runs at. Boundary only: the
 * stream open records it and a future boundary resampler will need it. No
 * synthesis path may read it. */
extern float g_deviceRate;
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
	char themeFile[256];
} Settings;

void createSettings(Settings *s);
#endif
