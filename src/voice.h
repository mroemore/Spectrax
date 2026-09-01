#ifndef VOICE_H
#define VOICE_H
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include "kiss_fft.h"
#include "settings.h"
#include "oscillator.h"
#include "sample.h"
#include "notes.h"
#include "modsystem.h"
#include "blit_synth.h"
#include "filters.h"
#include "fft.h"

#define MAX_LFOS 8
#define MAX_ENVELOPES 6
#define MAX_FM_OPERATORS 4
#define MAX_DETUNE 16
#define MAX_PATCHES 255
/* Task 8: the bank presents a fixed navigable slot count. Slots beyond
 * presetCount (the on-disk presets) hold a default FM patch (see
 * makeDefaultFmPreset) so PREV/NEXT can walk into blank slots and the
 * user can load a preset into them or edit + save a fresh one. */
#define PRESET_BANK_SLOTS 16

typedef enum {
	VOICE_TYPE_SAMPLE,
	VOICE_TYPE_FM,
	VOICE_TYPE_BLEP,
	VOICE_TYPE_GRAIN,
	VOICE_TYPE_SPECTRAL,
	VOICE_TYPE_COUNT
} VoiceType;

typedef enum {
	PRESET_OK,
	PRESET_EXISTS,
	PRESET_ERROR_OPEN,
	PRESET_ERROR_READ,
	PRESET_ERROR_WRITE,
	PRESET_ERROR_FORMAT,
	PRESET_ERROR_MEMORY
} PresetFileResult;

typedef struct {
	float L;
	float R;
} OutVal;

#define GRANULAR_BUFFER_SIZE 441000 // 10 seconds
#define GRAIN_COUNT 16
#define GRAIN_WINDOW_SIZE 1024

typedef struct Voice Voice;
typedef OutVal (*GenerateSample)(Voice *currentVoice, float phaseIncrement, float frequency);

typedef struct {
	ParamList *paramList;
	ModList *modList;
	float buffer[GRANULAR_BUFFER_SIZE];
	float grainWindow[GRAIN_WINDOW_SIZE];
	int windowIndex[GRAIN_WINDOW_SIZE];
	int writeHead;
	Parameter *grainStartPos[GRAIN_COUNT];
	float grainReadPos[GRAIN_COUNT];
	Parameter *grainVelocity;
	Parameter *grainMs;
	Parameter *volume;
	Sample *sample;
	Envelope *mainEnv;
	Envelope *grainEnvs[GRAIN_COUNT];

} GranularProcessor;

GranularProcessor *createGranularProcessor(Sample *s);
OutVal granularProcess(GranularProcessor *gp, float phaseIncrement);

typedef struct {
	Sample *sample;
} GranularInstrumentData;

typedef struct {
	int shape;
} BlepPatch;

typedef struct {
	Parameter *shape;
} BlepInstrumentData;

typedef struct {
	OperatorData ops[MAX_FM_OPERATORS];
	int selectedAlgorithm;
} FmPatch;

typedef struct {
	Operator *ops[MAX_FM_OPERATORS];
	Parameter *selectedAlgorithm;
} FmInstrumentData;

typedef struct {
	Sample *sample;
	Parameter *playbackSpeed;
	SamplePool *sp;
	float *spectralData;
	int spectralDataSize;
} SpectralInstrumentData;

typedef struct {
	int bitDepth;
	int sampleRate;
	bool loopSample;
	int sampleIndex;
	SamplePlaybackType playbackType;
	int loopStartIndex;
	int loopEndIndex;
} SamplerPatch;

typedef struct {
	Sample *sample;
	SamplePool *sp;
	Parameter *bitDepth;
	Parameter *sampleRate;
	Parameter *loopSample;
	Parameter *sampleIndex;
	Parameter *playbackType;
	Parameter *loopStartIndex;
	Parameter *loopEndIndex;
	GetSampleFunc getSampleValue;
} SamplerInstrumentData;

typedef struct {
	char name[33];
	VoiceType voiceType;
	ModPreset modSettings[MAX_ENVELOPES + MAX_LFOS];
	int modSettingsCount;
	union {
		SamplerPatch sampler;
		FmPatch fm;
		BlepPatch blep;
	} pd;
} Preset;

typedef struct {
	Preset patches[MAX_PATCHES];
	int presetCount;
} PresetBank;

/* Task 6: dirty tracking. A LoadedPreset records the on-disk
 * snapshot that the instrument's live state was last loaded from
 * (or last saved to). The dirty bit is what makes the snapshot
 * meaningful: true means the live state has been edited since the
 * capture and now differs from disk.
 *
 * Layout follows the brief exactly:
 *   - `snapshot`: full Preset captured at load/save time, used to
 *     diff against the live instrument (see isInstrumentDirty).
 *     Zero-initialised at init so a brand-new instrument has a
 *     known (zero) baseline rather than reading uninitialised bytes.
 *   - `dirty`: live-edit flag. Set true by the KM_EDIT+arrow dial
 *     dispatch (main.c + harness). Cleared to false by
 *     markPresetLoaded whenever the live state matches disk again.
 *   - `name`: a fixed-size (33 byte) copy of the preset name so
 *     consumers can read it without chasing the bank pointer. The
 *     canonical identity in the snapshot above is a separate field
 *     and uses the same width — we keep both for parity with the
 *     brief while making the bank lookup cheap. */
typedef struct {
	Preset snapshot;
	bool dirty;
	char name[33];
} LoadedPreset;

/* Forward-declare VoiceManager so Instrument can hold a back-pointer;
 * the full definition appears further down. */
typedef struct VoiceManager VoiceManager;

typedef struct {
	ModList *modList;
	ParamList *paramList;
	Envelope *envelopes[MAX_ENVELOPES];
	int envelopeCount;
	int coreEnvelopeCount;
	int lfoCount;
	int patchIndex;
	float volumeAttenuation;
	VoiceType voiceType;
	Parameter *detuneVoiceCount;
	Parameter *detuneRange;
	Parameter *detuneSpread;
	Parameter *panning;
	PresetBank *presetBank;
	Parameter *selectedPresetIndex;
	/* Task 6: snapshot of the last loaded/saved preset. The `dirty`
	 * sub-field is the live-edit flag — set true by the KM_EDIT+arrow
	 * hook (any time a dial callback fires), cleared to false by
	 * markPresetLoaded whenever the instrument's state is brought
	 * back in sync with disk (after a load or a successful save).
	 * See LoadedPreset above for the full layout. */
	LoadedPreset loaded;
	VoiceManager *vm;
	/* Channel index used by the voice-count dial's onChange callback
	 * to look up the right channel in the VoiceManager. Assigned by
	 * createVoiceManager (boot) and setInstrumentVoiceType (type
	 * change rebuilds the Instrument so the field is refreshed).
	 * -1 means "unset" (e.g. during construction between the malloc
	 * and the index assignment). */
	int metaChannel;
	/* Set by the GUI thread while the instrument's param/mod lists and
	 * voice pool are being rebuilt (applyInstrumentPreset / voice
	 * rebuild). The PortAudio callback checks it and skips this channel
	 * while true -- without it, the audio thread derefs freed params
	 * mid-rebuild and segfaults on preset change (e.g. NEXT). */
	volatile bool rebuilding;
	/* Task 6: instrument-page meta-row voice-count dial. Range 1..8
	 * (clamped by Parameter on every write). Synced to
	 * vm->voiceCount[channel] via the onChange callback
	 * (`cb_setVoiceCount`); both paths write the param's baseValue too
	 * so a programmatic setChannelVoiceCount can't desync the
	 * displayed dial from the live voice pool. Owned by the instrument's
	 * paramList (freed with the instrument). */
	Parameter *voiceCountParam;
	union {
		SamplerInstrumentData sampler;
		FmInstrumentData fm;
		SpectralInstrumentData spectral;
		BlepInstrumentData blep;
		GranularInstrumentData granular;
	} id;
} Instrument;

typedef struct {
	Oscillator oscillator;
} BlepVoiceData;

typedef struct {
	Operator *operators[MAX_FM_OPERATORS];
} FmVoiceData;

typedef struct {
	Sample *sample;
	float *spectralData;
	float samplePosition;
	int spectralDataSize;
} SpectralVoiceData;

typedef struct {
	Sample *sample;
	float samplePosition; // Position in the sample data
	SamplePool *samplePool;
} SamplerVoiceData;

typedef struct {
	GranularProcessor *granularProcessor;
} GranularVoiceData;

struct Voice {
	VoiceType type;
	float leftPhase;
	float rightPhase;
	float detunePhase[MAX_DETUNE];
	int note[2];
	int samplesElapsed;
	int active;
	ParamList *paramList;
	ModList *modList;
	int envCount;
	int lfoCount;
	Envelope *envelope[4];
	LFO *lfo[2];
	Parameter *frequency;
	Parameter *volume;
	Instrument *instrumentRef;
	GenerateSample generate;
	union {
		FmVoiceData fm;
		BlepVoiceData blep;
		SpectralVoiceData spectral;
		SamplerVoiceData sampler;
		GranularVoiceData granular;
	} vd;
	Filter *filter;
};

typedef enum {
	VA_FREE_OR_ZERO,
	VA_FREE_OR_OLDEST,
	VA_ROUND_ROBIN,
	VA_RANDOM
} AllocationBehaviour;

typedef struct VoiceManager {
	Voice *voicePools[MAX_SEQUENCER_CHANNELS][MAX_VOICES_PER_CHANNEL];
	Instrument *instruments[MAX_SEQUENCER_CHANNELS];
	VoiceType voiceTypes[MAX_SEQUENCER_CHANNELS];
	int voiceCount[MAX_SEQUENCER_CHANNELS];
	int enabledChannels;
	WavetablePool *wavetablePool;
	SamplePool *samplePool;
	AllocationBehaviour voiceAllocation[MAX_SEQUENCER_CHANNELS];
} VoiceManager;

/* Audio/GUI thread hand-off. The PortAudio callback locks this for the
 * whole buffer's processing; every GUI-thread mutation of the instrument
 * param/mod lists or the voice pool (type swap, preset apply, voice-count
 * resize, route/delete) holds it across its free+rebuild window. Without
 * it the `rebuilding` flag is a soft guard that still lets the audio
 * thread deref a freed voice/list (check-then-use race). */
extern pthread_mutex_t g_audioLock;

VoiceManager *createVoiceManager(Settings *settings, SamplePool *sp, WavetablePool *wtp, PresetBank *pb);
void initVoicePool(VoiceManager *vm, int channelIndex, int voiceCount, Instrument *inst);
void initVoiceManager(VoiceManager *vm, SamplePool *sp);
void freeVoice(Voice *v);
void freeVoiceManager(VoiceManager *vm);
/* Re-run createVoice / createParamPointerAD for every live voice on
 * `vm`'s channels whose instrument matches `inst`, so that envelope
 * stage params and FM operator params alias the new instrument's
 * freshly-allocated Param structs after a runtime preset change. */
void rebuildVoicesForInstrument(VoiceManager *vm, Instrument *inst);
Voice *getFreeVoice(VoiceManager *vm, int seqChannel);
/* Task 2: instrument-chip meta primitives. Both set the channel
 * instrument's `rebuilding` flag during the swap/resize so the audio
 * thread skips the channel while params/voices are being torn down
 * and rebuilt (see Instrument::rebuilding for the audio-thread side).
 * `setInstrumentVoiceType` swaps the instrument to a fresh one of the
 * requested VoiceType (defaults, no preset loaded) and frees the old
 * instrument. `setChannelVoiceCount` re-allocates the channel's voice
 * pool, clamped to [1, MAX_VOICES_PER_CHANNEL]. */
bool setInstrumentVoiceType(VoiceManager *vm, int channel, VoiceType vt);
bool setChannelVoiceCount(VoiceManager *vm, int channel, int count);
void triggerVoice(Voice *voice, int note[NOTE_INFO_SIZE]);
OutVal generateVoice(VoiceManager *vm, Voice *currentVoice, float phaseIncrement, float frequency);

void initDefaultFmPreset(Preset *p);
void applyInstrumentPreset(Instrument *instrument, Preset p);
Preset presetFromInstrument(Instrument *instrument);
void cb_setInstrumentPreset(void *instrument);
void initPresetBank(PresetBank *pb);
void addPresetToBank(PresetBank *pb, Preset p);
Preset makeDefaultFmPreset(void);
void fillEmptyBankSlots(PresetBank *pb);

/* Task 6: stamp `inst->loaded` with the on-disk identity of the
 * preset the instrument was just loaded from or saved to. Called
 * from applyInstrumentPreset (load refresh) and from the save
 * paths in gui.c (initial save + overwrite branch in
 * handlePresetUiInput). `name` is the preset's on-disk name; pass
 * NULL/"" to skip the capture. The capture snapshots the live
 * instrument state (via presetFromInstrument) into inst->loaded.snapshot,
 * copies the name into inst->loaded.name, and clears the dirty bit
 * so the next edit starts a fresh diff. */
void markPresetLoaded(Instrument *inst, const char *name);
bool isInstrumentDirty(const Instrument *inst);

void initialize_voice(Voice *voice, Instrument *inst);
void initInstDefaults(Instrument *i);
void init_instrument(Instrument **instrument, VoiceType vt, SamplePool *samplePool, PresetBank *pb);
void initInstrumentFromPreset(Instrument **instrument, SamplePool *samplePool, Preset p);
void setSamplePlaybackFunction(void *instrument);
void updateSampleReferences(void *instrument);
#endif // VOICE_H
