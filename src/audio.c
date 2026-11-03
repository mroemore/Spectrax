/* audio.c — the real-time audio render path.
 *
 * Split out of main.c so it is a translation unit a test can link and drive.
 * The whole buffer-render loop used to live inside `static int
 * patestCallback(...)`, which meant the only way to execute it was to start a
 * real PortAudio stream — so the concurrency between this code and the GUI
 * thread's instrument rebuilds was untestable and therefore unguarded by
 * anything but a comment.
 *
 * renderAudioBuffer() is that body verbatim, with the PortAudio signature
 * adaptation left behind in main.c's adapter. It keeps the lock, because the
 * lock is part of the contract: every deref below of the instrument lists and
 * voice pool is shared with the GUI thread, whose type-swap / preset-apply /
 * resize paths free those objects under the same lock.
 *
 * Owns the master volume and the --probe-tempo cadence counters, since both
 * are read here and written from main.c's input/argv handling. */

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

#include "raylib.h"

#include "audio.h"
#include "main.h"
#include "modsystem.h"
#include "notes.h"
#include "sequencer.h"
#include "voice.h"
#include "vizfx.h"
#include "fft.h"

/* ---- master volume (set from --volume / -V, or the volume keybinds) ----
 * Atomic, not plain: the audio thread reads this once per stereo frame while
 * the GUI thread may write it. A single 4-byte aligned load/store is what the
 * race detector needs and all it is, so a relaxed atomic costs nothing on the
 * hot path — whereas taking g_audioLock for a volume read would put a mutex
 * round-trip in the per-frame loop and invite lock-ordering trouble for no
 * benefit. (Caught by ThreadSanitizer; today the only writer is argv, which
 * runs before Pa_StartStream, but this is a public setter, so the next caller
 * — a volume keybind is the obvious one — must not inherit the race.) */
static _Atomic float g_volume = 1.0f;

void audioSetMasterVolume(float v) {
	atomic_store_explicit(&g_volume, v, memory_order_relaxed);
}

float audioGetMasterVolume(void) {
	return atomic_load_explicit(&g_volume, memory_order_relaxed);
}

/* ---- --probe-tempo diagnostic (rule #1136: zero effect without the flag).
 * Logs the tempo state once at startup, then whenever bpm or the step sample
 * counts change, plus a heartbeat every ~2s while playing. The step counter is
 * bumped in renderAudioBuffer, which is where steps actually happen. ---- */
static long g_tempoProbeSteps = 0;   /* increments once per sequencer step */
static double g_tempoProbeStartT = 0.0;
static long g_tempoProbeLastMs = 0;
static int g_lastProbedBpm = -1;
static int g_lastProbedEven = -1;
static int g_lastProbedOdd = -1;

bool g_probeTempo = false;

void probeTempoState(paTestData *data) {
	if(!g_probeTempo || !data || !data->arranger || !data->arranger->tempoSettings.bpm) {
		return;
	}
	Parameter *bpm = data->arranger->tempoSettings.bpm;
	Parameter *swing = data->arranger->tempoSettings.swing;
	int bv = (int)getParameterValue(bpm);
	int ev = data->arranger->tempoSettings.samplesPerEvenStep;
	int od = data->arranger->tempoSettings.samplesPerOddStep;
	int sw = swing ? (int)getParameterValue(swing) : -1;
	bool changed = (bv != g_lastProbedBpm || ev != g_lastProbedEven || od != g_lastProbedOdd);
	double now = GetTime();
	if(g_tempoProbeStartT == 0.0) {
		g_tempoProbeStartT = now;
	}
	/* log on change, plus a ~2s heartbeat while playing so a steady-state
	 * turbo cadence is captured even if nothing changes */
	bool heartbeat = data->arranger->playing && (now - g_tempoProbeLastMs > 2.0);
	if(changed || heartbeat) {
		double dt = now - g_tempoProbeStartT;
		double stepRate = (dt > 0.001) ? (double)g_tempoProbeSteps / dt : 0.0;
		double expectStepsPerSec = (double)bv / 60.0 * 4.0; /* 4 steps/beat at this bpm */
		fprintf(stderr, "TEMPO bpm=%d base=%.1f even=%d odd=%d swing=%d playing=%d elapsed=%d t=%.1f steps=%ld rate=%.1f expect=%.1f\n",
		        bv, bpm->baseValue, ev, od, sw, data->arranger->playing,
		        data->arranger->tempoSettings.samplesElapsed, now, g_tempoProbeSteps,
		        stepRate, expectStepsPerSec);
		g_tempoProbeLastMs = (long)now;
		g_lastProbedBpm = bv;
		g_lastProbedEven = ev;
		g_lastProbedOdd = od;
	}
}

/* advanceSequencerStep trigger callback: allocate a voice for the fired
 * note. Runs on the audio thread inside the render lock. */
static void triggerSequencerNote(void *ctx, int channel, const int *note, int velocity) {
	VoiceManager *vm = (VoiceManager *)ctx;
	Voice *voice = getFreeVoice(vm, channel);
	triggerVoice(voice, note, velocity);
}

void renderAudioBuffer(paTestData *data, float *out, unsigned long framesPerBuffer) {
	unsigned int i = 0;
	unsigned int j = 0;
	/* Hold the audio lock for the whole buffer: every deref of the
	 * instrument lists / voice pool below is shared with the GUI thread,
	 * whose type-swap / preset-apply / resize paths free them under the
	 * same lock. Without it the `rebuilding` flag leaves a check-then-use
	 * window where the audio thread touches a freed voice (type-cycle
	 * segfault). Non-contended in practice — the GUI only takes it during
	 * a reconfiguration, which skips a buffer at worst. */
	pthread_mutex_lock(&g_audioLock);
	float max_output = 0.0f;
	clock_t start, end;
	double cpu_time_used;
	/* The governor's own measurement: the wall-clock time this callback
	 * took, measured around it. clock() is not usable here -- it counts
	 * the CPU time of every thread in the process, so the GUI's rendering
	 * would be counted against the audio callback's deadline. */
	struct timespec wallStart;
	struct timespec wallEnd;
	clock_gettime(CLOCK_MONOTONIC, &wallStart);
	start = clock();
	/* The step boundary is checked inside the sample loop, not here: a
	 * note must fire on the sample it belongs to. Checking it once per
	 * buffer made the note fire at the next buffer's start -- up to a
	 * whole buffer late -- so the onset moved with the callback's buffer
	 * size. The buffer size is a latency knob, not a parameter of the
	 * sound. */
	for(i = 0; i < framesPerBuffer; i++) {
		int stepSamples = data->arranger->tempoSettings.swingStep ? data->arranger->tempoSettings.samplesPerOddStep : data->arranger->tempoSettings.samplesPerEvenStep;
		if(data->arranger->tempoSettings.samplesElapsed >= stepSamples) {
		data->arranger->tempoSettings.samplesElapsed = 0;
		if(data->arranger->playing) {
			if(g_probeTempo) {
				g_tempoProbeSteps++;
				/* Per-step log for the first 5s of probe runtime: captures
				 * the exact cadence + what triggers on each step so a
				 * turbo episode at startup is visible step-by-step even if
				 * it corrects itself before the 2s heartbeat. */
				double pn = GetTime();
				if(g_tempoProbeStartT == 0.0) {
					g_tempoProbeStartT = pn;
				}
				if(pn - g_tempoProbeStartT < 5.0) {
					fprintf(stderr, "STEP %ld bpm=%d swing=%d ssw=%d ev=%d od=%d stepSmp=%d\n",
					        g_tempoProbeSteps,
					        (int)getParameterValue(data->arranger->tempoSettings.bpm),
					        (int)getParameterValueAsInt(data->arranger->tempoSettings.swing),
					        data->arranger->tempoSettings.swingStep,
					        data->arranger->tempoSettings.samplesPerEvenStep,
					        data->arranger->tempoSettings.samplesPerOddStep,
					        stepSamples);
				}
			}
			advanceSequencerStep(data->sequencer, data->patternList, data->arranger, triggerSequencerNote, data->voiceManager);

			/* Pattern clock: tempo-synced mod sources read the per-channel
			 * pattern-step playhead + swing-aware step duration. Written
			 * here (inside the playing guard) so the playhead matches the
			 * just-advanced step and stopped playback leaves the clock
			 * frozen at its last value. The step duration must be derived
			 * AFTER the advance: incrementSequencer toggles swingStep
			 * inside, so swingStep now describes the step that just
			 * STARTED — reading the pre-advance stepSamples would give the
			 * mod the previous step's duration and skew LINEAR/CURVE
			 * progress under swing. */
			for(int ch = 0; ch < data->arranger->enabledChannels; ch++) {
				g_patternClock.playhead[ch] = data->sequencer->playhead_index[ch];
			}
			g_patternClock.stepDuration = data->arranger->tempoSettings.swingStep
				? data->arranger->tempoSettings.samplesPerOddStep
				: data->arranger->tempoSettings.samplesPerEvenStep;
		}
		}

		/* Task 2.4: audio comes solely from the per-voice mod graphs. The
		 * instrument's own modLists no longer run in the audio path (their
		 * sources/dests are the control plane; voices carry the copies and
		 * syncVoiceGraph pulls live baseValues every buffer).
		 *
		 * The song-level pass runs per sample, like the voices' own. It used
		 * to run once per buffer with deltaTime = 1/framesPerBuffer, which
		 * made the song-level mods advance by a buffer's worth of time at a
		 * time: their phase was a staircase whose step was the buffer size,
		 * and the buffer size was therefore a parameter of the sound. */
		processModulations(data->globalParameters, data->modList, 1.0f / SPECTRAX_ENGINE_RATE);

		float left_output = 0.0f;
		float right_output = 0.0f;

		for(j = 0; j < data->arranger->enabledChannels; j++) {
			/* Task 8: skip channels whose voice pool is mid-rebuild (the
			 * Voice structs are freed + re-malloc'd by rebuildVoicesForInstrument
			 * on preset change). Iterating voicePools[j][v] here would
			 * deref a freed Voice. */
			if(data->voiceManager->instruments[j]->rebuilding) {
				continue;
			}
			for(int v = 0; v < data->voiceManager->voiceCount[j]; v++) {
				Voice *currentVoice = data->voiceManager->voicePools[j][v];
				if(currentVoice->active) {
					/* Task 2.3: live-base sync from the instrument before
					 * this voice's per-sample modulation pass. */
					syncVoiceGraph(currentVoice);
					processModulations(currentVoice->paramList, currentVoice->modList, 1.0f / SPECTRAX_ENGINE_RATE);
					// Handle envelope
					Mod *gainDriver = (currentVoice->cloneCount > 0) ? currentVoice->clones[0] : NULL;
					if(gainDriver && gainDriver->type == MT_ENV && !gainDriver->data.env.isTriggered) {
						setParameterValue(currentVoice->volume, 1.0f);
						setParameterBaseValue(currentVoice->volume, 1.0f);
						currentVoice->active = 0;
						// printf("%i not triggered...", j);
						if(currentVoice->type == VOICE_TYPE_SAMPLE) {
							currentVoice->vd.sampler.samplePosition = 0.0f;
						}
					}

					float phase_increment = 0.0f;
					float freq = 0.0f;
					if(currentVoice->note[0] != OFF) {
						freq = noteFrequencies[currentVoice->note[0]][currentVoice->note[1]];
						setParameterBaseValue(currentVoice->frequency, freq);
						setParameterValue(currentVoice->frequency, freq);
						phase_increment = freq / SPECTRAX_ENGINE_RATE;
					}

					OutVal currentSample = generateVoice(data->voiceManager, currentVoice, phase_increment, freq);

					/* Voices always mix while active so that stopping the
					 * song lets the current notes ring out through their
					 * AD envelope (which ends at 0 amplitude) instead of
					 * hard-zeroing the output mid-cycle. The old
					 * `playing ? mix : zero` branch snapped every active
					 * voice's sample to 0 the instant `playing` flipped --
					 * a step discontinuity, i.e. the click/pop heard on
					 * start and stop. */
					left_output += currentSample.L * getParameterValue(currentVoice->volume);
					right_output += currentSample.R * getParameterValue(currentVoice->volume);
					currentVoice->leftPhase = fmodf(currentVoice->leftPhase + phase_increment, 1.0f);
					currentVoice->rightPhase = fmodf(currentVoice->rightPhase + phase_increment, 1.0f);

					currentVoice->samplesElapsed++;
					if(!data->arranger->playing) {
						currentSample.L = 0;
						currentSample.R = 0;
						left_output = 0.0f;
						right_output = 0.0f;
					}
				} else {
					//	data->voices[j].mod[0].result = 0;
				}
			}
		}

		// Track the maximum output value
		if(fabsf(left_output) > max_output)
			max_output = fabsf(left_output);
		if(fabsf(right_output) > max_output)
			max_output = fabsf(right_output);

		float vol = audioGetMasterVolume();
		*out++ = left_output * vol;
		*out++ = right_output * vol;

		pushFrameToFFT(&data->spectrogram.fft, left_output);

		pushBufferScrollerFrame(&data->bufferScroller, left_output);
		pushMixRingSample(&data->mixRing, left_output);

		data->arranger->tempoSettings.samplesElapsed++;
	}

	processFFTData(&data->spectrogram.fft);

	// Normalize the entire buffer to avoid clipping
	//  if (max_output > MAX_VOLUME)
	//  {
	//  	float normalization_factor = MAX_VOLUME / max_output;
	//  	out = (float *)outputBuffer;
	//  	for (i = 0; i < framesPerBuffer * 2; i++)
	//  	{
	//  		*out++ *= normalization_factor;
	//  	}
	//  }
	end = clock();
	cpu_time_used = (((double)(end - start)) / CLOCKS_PER_SEC) * 1000.0f;
	pushTimeGraphMeasurement(&data->timeGraph, cpu_time_used);

	/* The governor: how much of its deadline the callback used, as a
	 * fraction. The deadline is the buffer's duration at the engine's
	 * rate, so a callback that finishes in time scores below 1.
	 *
	 * The measurement is the callback's own wall-clock duration, not the
	 * process's CPU time: the GUI renders on another thread, and its
	 * cost is not the audio callback's. */
	clock_gettime(CLOCK_MONOTONIC, &wallEnd);
	if(data->voiceManager) {
		double elapsedMs = ((double)(wallEnd.tv_sec - wallStart.tv_sec) * 1000.0) +
		                   ((double)(wallEnd.tv_nsec - wallStart.tv_nsec) / 1000000.0);
		double deadlineMs = (1000.0 * (double)framesPerBuffer) / (double)SPECTRAX_ENGINE_RATE;
		if(deadlineMs > 0.0) {
			governorObserve(&data->voiceManager->governor, elapsedMs / deadlineMs);
		}
	}
	pthread_mutex_unlock(&g_audioLock);
}