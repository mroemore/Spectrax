#include "alsa_output.h"

#include "alsa_min.h"
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <time.h>

#include "settings.h" /* SPECTRAX_ENGINE_RATE */

#define ALSA_OUT_CHANNELS 2
/* Four periods in the device buffer: enough that one late write does not
 * underrun, small enough that stop/drop is prompt. */
#define ALSA_OUT_PERIODS 4

typedef struct {
	snd_pcm_t *pcm;
	paTestData *data;
	unsigned frames;
	unsigned reqFrames;   /* the buffer the caller asked for (see the accessors) */
	snd_pcm_format_t format;
	float *mix;      /* engine output, interleaved */
	void *converted; /* S32 or S16 buffer */
	int rate;        /* the configuration in force (the exact rate, verified) */
	snd_pcm_uframes_t period;
	snd_pcm_uframes_t buffer;
	volatile int running;
	AlsaOutputStats stats;
} AlsaOutput;

static AlsaOutput out;
static pthread_t outThread;
static int threadStarted = 0;

/* --- conversions (also the unit-tested part) ---------------------------- */

void alsaOutputConvertFloatToS32(const float *in, int32_t *out, size_t samples) {
	for(size_t i = 0; i < samples; i++) {
		float v = in[i];
		if(v > 1.0f) {
			v = 1.0f;
		} else if(v < -1.0f) {
			v = -1.0f;
		} else if(v != v) { /* NaN */
			v = 0.0f;
		}
		/* Scale in double and clamp explicitly: 2147483647.0f is not a float
		 * (the nearest is 2147483648.0f), so a float multiply at full scale
		 * rounds past INT32_MAX and the cast becomes implementation-defined.
		 * Symmetric full scale, so a value past either rail clamps rather
		 * than wrapping (a wrapped sample is a full-scale click). */
		double scaled = (double)v * 2147483647.0;
		if(scaled > 2147483647.0) {
			scaled = 2147483647.0;
		} else if(scaled < -2147483647.0) {
			scaled = -2147483647.0;
		}
		out[i] = (int32_t)llrint(scaled);
	}
}

void alsaOutputConvertFloatToS16(const float *in, int16_t *out, size_t samples) {
	for(size_t i = 0; i < samples; i++) {
		float v = in[i];
		if(v > 1.0f) {
			v = 1.0f;
		} else if(v < -1.0f) {
			v = -1.0f;
		} else if(v != v) {
			v = 0.0f;
		}
		out[i] = (int16_t)lrintf(v * 32767.0f);
	}
}

/* ALSA's own diagnostics on this CFW are a wall of "Unknown PCM cards.pcm.*"
 * at every open. They are config noise, not our error, and stderr is the app's
 * log; the failures this module cares about are reported through err. */
static void quietAlsaErrors(const char *file, int line, const char *func, int err,
                            const char *fmt, ...) {
	(void)file;
	(void)line;
	(void)func;
	(void)err;
	(void)fmt;
}

/* --- device setup ------------------------------------------------------- */

/* Set the parameters, then read the rate back and refuse a substitution: the
 * engine is fixed at SPECTRAX_ENGINE_RATE, and PortAudio's silent rate swap is
 * exactly the failure this backend exists to remove. */
static bool configure(snd_pcm_t *pcm, const char *pcmName, unsigned frames,
                      snd_pcm_format_t *formatOut, int *rateOut,
                      snd_pcm_uframes_t *periodOut, snd_pcm_uframes_t *bufferOut,
                      char *err, size_t errLen) {
	snd_pcm_hw_params_t *params = NULL;
	if(snd_pcm_hw_params_malloc(&params) < 0) {
		snprintf(err, errLen, "cannot allocate ALSA hw params");
		return false;
	}

	int rc = snd_pcm_hw_params_any(pcm, params);
	if(rc >= 0) {
		rc = snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
	}
	/* The H700 codec takes S32_LE (24-bit payload) or S16_LE, never float. */
	snd_pcm_format_t format = SND_PCM_FORMAT_S32_LE;
	if(rc >= 0 && snd_pcm_hw_params_test_format(pcm, params, format) < 0) {
		format = SND_PCM_FORMAT_S16_LE;
	}
	if(rc >= 0) {
		rc = snd_pcm_hw_params_set_format(pcm, params, format);
	}
	if(rc >= 0) {
		rc = snd_pcm_hw_params_set_channels(pcm, params, ALSA_OUT_CHANNELS);
	}
	unsigned wantRate = SPECTRAX_ENGINE_RATE;
	if(rc >= 0) {
		rc = snd_pcm_hw_params_set_rate(pcm, params, wantRate, 0);
	}
	/* The period is the engine buffer and therefore the output latency, so ask
	 * for exactly that many frames (and a buffer of a few periods). _near is
	 * only the fallback: hw:0,0 defaults to a 32768-frame period (0.68 s),
	 * which `_near` happily returns for a 512-frame request. */
	snd_pcm_uframes_t period = frames;
	snd_pcm_uframes_t bufferSize = (snd_pcm_uframes_t)frames * ALSA_OUT_PERIODS;
	int exactPeriod = 0;
	int exactBuffer = 0;
	if(rc >= 0) {
		exactPeriod = (snd_pcm_hw_params_set_period_size(pcm, params, period, 0) >= 0);
		if(!exactPeriod) {
			period = frames;
			exactPeriod = (snd_pcm_hw_params_set_period_size_near(pcm, params, &period, NULL) >= 0);
		}
	}
	if(rc >= 0) {
		exactBuffer = (snd_pcm_hw_params_set_buffer_size(pcm, params, bufferSize) >= 0);
		if(!exactBuffer) {
			bufferSize = (snd_pcm_uframes_t)frames * ALSA_OUT_PERIODS;
			exactBuffer = (snd_pcm_hw_params_set_buffer_size_near(pcm, params, &bufferSize) >= 0);
		}
	}
	if(rc >= 0 && (!exactPeriod || !exactBuffer)) {
		fprintf(stderr, "audio: %s will not take a %u-frame period / %lu-frame buffer exactly; "
		        "the driver picks its own latency\n", pcmName, frames,
		        (unsigned long)frames * ALSA_OUT_PERIODS);
	}
	if(rc >= 0) {
		rc = snd_pcm_hw_params(pcm, params);
	}
	if(rc < 0) {
		snprintf(err, errLen, "cannot set %s to %d Hz/%d ch/%s: %s", pcmName,
		         (int)wantRate, ALSA_OUT_CHANNELS, snd_pcm_format_name(format),
		         snd_strerror(rc));
		snd_pcm_hw_params_free(params);
		return false;
	}

	/* Read back the configuration in force. The accessors need a params object
	 * filled by snd_pcm_hw_params_current(); the object used to SET the values
	 * does not report them (it reads back as 0 Hz, which looks exactly like a
	 * substituted rate). hw:0,0 exposes no rate read-back at all -- there the
	 * guarantee is the exact set_rate above: with dir=0 the PCM either takes
	 * exactly SPECTRAX_ENGINE_RATE or snd_pcm_hw_params() fails. That is the
	 * property PortAudio's silent substitution broke. */
	unsigned gotRate = 0;
	unsigned gotChannels = 0;
	snd_pcm_uframes_t gotPeriod = 0;
	snd_pcm_uframes_t gotBuffer = 0;
	if(snd_pcm_hw_params_current(pcm, params) >= 0) {
		snd_pcm_hw_params_get_rate(params, &gotRate, NULL);
		snd_pcm_hw_params_get_channels(params, &gotChannels);
		snd_pcm_hw_params_get_period_size(params, &gotPeriod, NULL);
		snd_pcm_hw_params_get_buffer_size(params, &gotBuffer);
	}
	snd_pcm_hw_params_free(params);
	if((gotRate != 0 && gotRate != wantRate) ||
	   (gotChannels != 0 && (int)gotChannels != ALSA_OUT_CHANNELS)) {
		snprintf(err, errLen, "%s runs at %u Hz/%u ch, the engine runs at %d Hz/%d ch",
		         pcmName, gotRate, gotChannels, (int)wantRate, ALSA_OUT_CHANNELS);
		return false;
	}
	*formatOut = format;
	*rateOut = (int)wantRate;
	*periodOut = gotPeriod;
	*bufferOut = gotBuffer;
	return true;
}

/* --- the write loop ----------------------------------------------------- */

static int writeFrames(const float *mix, unsigned frames) {
	unsigned samples = frames * ALSA_OUT_CHANNELS;
	if(out.format == SND_PCM_FORMAT_S32_LE) {
		alsaOutputConvertFloatToS32(mix, (int32_t *)out.converted, samples);
	} else {
		alsaOutputConvertFloatToS16(mix, (int16_t *)out.converted, samples);
	}

	unsigned done = 0;
	while(done < frames && out.running) {
		/* Blocking write: this is the clock. It returns when the device has
		 * room, i.e. one period of real audio has passed. */
		snd_pcm_sframes_t rc = snd_pcm_writei(out.pcm, out.converted,
		                                       (snd_pcm_uframes_t)(frames - done));
		if(rc == -EAGAIN) {
			/* Only in non-blocking mode, which this is not; recover anyway. */
			if(snd_pcm_wait(out.pcm, 100) < 0) {
				return -1;
			}
			continue;
		}
		if(rc == -EPIPE) {
			/* Underrun: the device ran dry (or we were suspended). Re-arm and
			 * keep the new period; the transport clock just skipped, which is
			 * what an overrun does at the hardware level. */
			out.stats.xruns++;
			snd_pcm_prepare(out.pcm);
			continue;
		}
		if(rc == -ESTRPIPE) {
			while(snd_pcm_resume(out.pcm) == -EAGAIN) {
				struct timespec pause = { 0, 10 * 1000 * 1000 };
				nanosleep(&pause, NULL);
			}
			snd_pcm_prepare(out.pcm);
			continue;
		}
		if(rc < 0) {
			out.stats.errors++;
			if(snd_pcm_recover(out.pcm, (int)rc, 1) < 0) {
				return -1;
			}
			continue;
		}
		done += (unsigned)rc;
	}
	return 0;
}

static void *threadBody(void *arg) {
	(void)arg;
	/* Name it so a device-side `ps -L`/top shows which thread holds the PCM. */
	prctl(PR_SET_NAME, (unsigned long)"spx-alsa-out", 0, 0, 0);

	while(out.running) {
		/* The same render path the PortAudio callback uses: one buffer of
		 * engine samples, sample-accurate steps included. */
		renderAudioBuffer(out.data, out.mix, out.frames);
		out.stats.writes++;
		out.stats.frames += out.frames;
		if(writeFrames(out.mix, out.frames) != 0) {
			break;
		}
	}
	return NULL;
}

/* --- public API --------------------------------------------------------- */

bool alsaOutputStart(paTestData *data, const char *pcmName, unsigned frameCount,
                     char *err, size_t errLen) {
	if(!data || !pcmName || !*pcmName || frameCount == 0) {
		snprintf(err, errLen, "invalid ALSA output request");
		return false;
	}
	if(threadStarted) {
		return true;
	}
	if(errLen) {
		err[0] = '\0';
	}
	memset(&out, 0, sizeof(out));
	out.data = data;
	out.frames = frameCount;
	/* The requested geometry, for the accessors: a driver that exposes no
	 * read-back leaves period/buffer zero while still running at these. */
	out.reqFrames = frameCount;

	/* ALSA's error output is extremely chatty on this CFW (every missing
	 * cards.pcm.* plugin is printed at open); the app's stderr is its log. */
	snd_lib_error_set_handler(quietAlsaErrors);

	/* The launcher stops the session manager first, but stopping it is
	 * asynchronous: the codec can stay busy for a moment. Retry rather than
	 * losing the whole direct path (and the ramp) to that race. */
	int rc = -EBUSY;
	for(int attempt = 0; attempt < 20; attempt++) {
		rc = snd_pcm_open(&out.pcm, pcmName, SND_PCM_STREAM_PLAYBACK, 0);
		if(rc != -EBUSY && rc != -EAGAIN) {
			break;
		}
		struct timespec pause = { 0, 100 * 1000 * 1000 };
		nanosleep(&pause, NULL);
	}
	if(rc < 0) {
		snprintf(err, errLen, "cannot open ALSA PCM %s: %s", pcmName, snd_strerror(rc));
		return false;
	}
	if(!configure(out.pcm, pcmName, frameCount, &out.format, &out.rate, &out.period,
	              &out.buffer, err, errLen)) {
		snd_pcm_close(out.pcm);
		out.pcm = NULL;
		return false;
	}
	out.mix = (float *)calloc((size_t)frameCount * ALSA_OUT_CHANNELS, sizeof(float));
	size_t sampleBytes = (out.format == SND_PCM_FORMAT_S32_LE) ? sizeof(int32_t) : sizeof(int16_t);
	out.converted = calloc((size_t)frameCount * ALSA_OUT_CHANNELS, sampleBytes);
	if(!out.mix || !out.converted) {
		free(out.mix);
		free(out.converted);
		out.mix = NULL;
		out.converted = NULL;
		snd_pcm_close(out.pcm);
		out.pcm = NULL;
		snprintf(err, errLen, "out of memory for the ALSA buffers");
		return false;
	}

	rc = snd_pcm_prepare(out.pcm);
	if(rc < 0) {
		snprintf(err, errLen, "cannot prepare %s: %s", pcmName, snd_strerror(rc));
		free(out.mix);
		free(out.converted);
		snd_pcm_close(out.pcm);
		out.pcm = NULL;
		return false;
	}

	out.running = 1;
	if(pthread_create(&outThread, NULL, threadBody, NULL) != 0) {
		out.running = 0;
		free(out.mix);
		free(out.converted);
		snd_pcm_close(out.pcm);
		out.pcm = NULL;
		snprintf(err, errLen, "cannot start the ALSA output thread");
		return false;
	}
	threadStarted = 1;
	/* hw:0,0 does not report its period/buffer back through the params
	 * accessors (0 on the device, real sizes on a desktop PCM), so say which
	 * numbers these are instead of logging a bare 0. */
	int readBack = (out.period != 0 && out.buffer != 0);
	fprintf(stderr, "audio: ALSA %s %d Hz/%d ch %s, period=%lu buffer=%lu frames%s\n", pcmName,
	        out.rate, ALSA_OUT_CHANNELS,
	        out.format == SND_PCM_FORMAT_S32_LE ? "S32_LE" : "S16_LE",
	        readBack ? (unsigned long)out.period : (unsigned long)frameCount,
	        readBack ? (unsigned long)out.buffer : (unsigned long)frameCount * ALSA_OUT_PERIODS,
	        readBack ? "" : " (requested; the driver exposes no read-back)");
	return true;
}

void alsaOutputStop(void) {
	if(!threadStarted) {
		return;
	}
	out.running = 0;
	/* Drop the PCM so a thread parked inside snd_pcm_writei returns instead of
	 * waiting for the next period; then join before closing. */
	if(out.pcm) {
		snd_pcm_drop(out.pcm);
	}
	pthread_join(outThread, NULL);
	threadStarted = 0;
	if(out.pcm) {
		snd_pcm_close(out.pcm);
		out.pcm = NULL;
	}
	free(out.mix);
	free(out.converted);
	out.mix = NULL;
	out.converted = NULL;
}

bool alsaOutputActive(void) {
	return threadStarted != 0;
}

void alsaOutputStats(AlsaOutputStats *stats) {
	if(!stats) {
		return;
	}
	stats->writes = out.stats.writes;
	stats->frames = out.stats.frames;
	stats->xruns = out.stats.xruns;
	stats->errors = out.stats.errors;
}

int alsaOutputRate(void) {
	return threadStarted ? out.rate : 0;
}

unsigned long alsaOutputPeriodFrames(void) {
	return out.period ? (unsigned long)out.period : (unsigned long)out.reqFrames;
}

unsigned long alsaOutputBufferFrames(void) {
	return out.buffer ? (unsigned long)out.buffer
	                  : (unsigned long)out.reqFrames * ALSA_OUT_PERIODS;
}
