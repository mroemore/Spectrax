#include <math.h>

#include "gui.h"
#include "theme.h"
#include "vizfx.h"
#include "voice.h"

static int sampleToRow(float v) {
	if(v < -1.0f) {
		v = -1.0f;
	}
	if(v > 1.0f) {
		v = 1.0f;
	}
	return (int)((v + 1.0f) * 0.5f * (float)(SCROLLER_COLUMN_HEIGHT - 1));
}

static float logScale(float v, float factor) {
	if(factor <= 1.0f) {
		return v;
	}
	return logf(1.0f + v * (factor - 1.0f)) / logf(factor);
}

static float compandSample(float v) {
	if(v >= 0.0f) {
		return logScale(v, SCROLLER_LOG_FACTOR);
	}
	return -logScale(-v, SCROLLER_LOG_FACTOR);
}

void collapseBufferToColumn(const float *samples, int bufferSize, unsigned char *lo, unsigned char *hi, int columnHeight, float gain) {
	for(int r = 0; r < columnHeight; r++) {
		int start = (int)((float)r * bufferSize / columnHeight);
		int end = (int)((float)(r + 1) * bufferSize / columnHeight);
		if(end <= start) {
			end = start + 1;
		}
		if(end > bufferSize) {
			end = bufferSize;
		}
		int loRow = sampleToRow(compandSample(samples[start] * gain));
		int hiRow = loRow;
		for(int i = start; i < end; i++) {
			int y = sampleToRow(compandSample(samples[i] * gain));
			if(y < loRow) {
				loRow = y;
			}
			if(y > hiRow) {
				hiRow = y;
			}
		}
		lo[r] = (unsigned char)loRow;
		hi[r] = (unsigned char)hiRow;
	}
}

void packScrollerColumn(const unsigned char *lo, const unsigned char *hi, int columnHeight, Color *out, Color waveColour) {
	for(int r = 0; r < columnHeight; r++) {
		out[r] = getColourScheme()->waveformBg;
	}
	for(int r = 0; r < columnHeight; r++) {
		for(int y = lo[r]; y <= hi[r]; y++) {
			out[columnHeight - 1 - y] = waveColour;
		}
	}
}

void initBufferScroller(BufferScroller *bs) {
	bs->image = GenImageColor(SCROLLER_WIDTH, SCROLLER_COLUMN_HEIGHT, getColourScheme()->waveformBg);
	bs->texture = LoadTextureFromImage(bs->image);
	SetTextureFilter(bs->texture, TEXTURE_FILTER_POINT);
	bs->writeColumn = 0;
	bs->pendingHead = 0;
	bs->pendingTail = 0;
	bs->pendingCount = 0;
	bs->framePos = 0;
	bs->peakIndex = 0;
	for(int i = 0; i < SCROLLER_GAIN_WINDOW; i++) {
		bs->peakRing[i] = 0.0f;
	}
}

void pushBufferScrollerFrame(BufferScroller *bs, float sample) {
	bs->staging[bs->framePos] = sample;
	bs->peakRing[bs->peakIndex] = fabsf(sample);
	bs->peakIndex = (bs->peakIndex + 1) % SCROLLER_GAIN_WINDOW;
	bs->framePos++;
	if(bs->framePos >= SCROLLER_BUFFER_SIZE) {
		bs->framePos = 0;
		float peak = 0.0f;
		for(int i = 0; i < SCROLLER_GAIN_WINDOW; i++) {
			if(bs->peakRing[i] > peak) {
				peak = bs->peakRing[i];
			}
		}
		float gain = 1.0f;
		if(peak > 0.0f) {
			gain = 1.0f / peak;
			if(gain > SCROLLER_MAX_GAIN) {
				gain = SCROLLER_MAX_GAIN;
			}
		}
		collapseBufferToColumn(bs->staging, SCROLLER_BUFFER_SIZE,
		                       bs->pendingLo[bs->pendingTail],
		                       bs->pendingHi[bs->pendingTail],
		                       SCROLLER_COLUMN_HEIGHT, gain);
		bs->pendingTail = (bs->pendingTail + 1) % SCROLLER_PENDING_CAPACITY;
		if(bs->pendingCount < SCROLLER_PENDING_CAPACITY) {
			bs->pendingCount++;
		} else {
			bs->pendingHead = (bs->pendingHead + 1) % SCROLLER_PENDING_CAPACITY;
		}
	}
}

void updateBufferScrollerData(BufferScroller *bs) {
	if(bs->pendingCount == 0) {
		return;
	}
	Color *px = (Color *)bs->image.data;
	const Color waveColour = getColourScheme()->vline;
	Color packed[SCROLLER_COLUMN_HEIGHT];
	while(bs->pendingCount > 0) {
		int col = bs->writeColumn;
		packScrollerColumn(bs->pendingLo[bs->pendingHead], bs->pendingHi[bs->pendingHead],
		                   SCROLLER_COLUMN_HEIGHT, packed, waveColour);
		for(int r = 0; r < SCROLLER_COLUMN_HEIGHT; r++) {
			px[r * SCROLLER_WIDTH + col] = packed[r];
		}
		UpdateTextureRec(bs->texture, (Rectangle){ col, 0, 1, SCROLLER_COLUMN_HEIGHT }, packed);
		bs->writeColumn = (bs->writeColumn + 1) % SCROLLER_WIDTH;
		bs->pendingHead = (bs->pendingHead + 1) % SCROLLER_PENDING_CAPACITY;
		bs->pendingCount--;
	}
}

void drawBufferScroller(BufferScroller *bs, Rectangle dest) {
	const int w = SCROLLER_WIDTH;
	const int h = SCROLLER_COLUMN_HEIGHT;
	float sx = dest.width / (float)w;
	float sy = dest.height / (float)h;
	int split = bs->writeColumn;
	if(split > 0 && split < w) {
		DrawTexturePro(bs->texture, (Rectangle){ split, 0, w - split, h },
		               (Rectangle){ dest.x, dest.y, (w - split) * sx, dest.height },
		               (Vector2){ 0, 0 }, 0.0f, WHITE);
		DrawTexturePro(bs->texture, (Rectangle){ 0, 0, split, h },
		               (Rectangle){ dest.x + (w - split) * sx, dest.y, split * sx, dest.height },
		               (Vector2){ 0, 0 }, 0.0f, WHITE);
	} else {
		DrawTexturePro(bs->texture, (Rectangle){ 0, 0, w, h }, dest,
		               (Vector2){ 0, 0 }, 0.0f, WHITE);
	}
}

void freeBufferScroller(BufferScroller *bs) {
	UnloadTexture(bs->texture);
	UnloadImage(bs->image);
}

static Color modStripColor(ModType type) {
	switch(type) {
		case MT_LFO:
			return getColourScheme()->modStripLfo;
		case MT_ENV:
			return getColourScheme()->modStripEnv;
		case MT_RND:
			return getColourScheme()->modStripRnd;
		case MT_OFS:
			return getColourScheme()->modStripOfs;
		default:
			return getColourScheme()->modStripDefault;
	}
}

void initModStrip(ModStrip *ms, Voice **voicePool, int voiceCount, int width, int height) {
	ms->voicePool = voicePool;
	ms->voiceCount = voiceCount;
	ms->width = width;
	ms->height = height;
	ms->pingActive = false;
	ms->ping = LoadRenderTexture(width, height);
	ms->pong = LoadRenderTexture(width, height);
	BeginTextureMode(ms->ping);
	ClearBackground(BLACK);
	EndTextureMode();
	BeginTextureMode(ms->pong);
	ClearBackground(BLACK);
	EndTextureMode();
}

static ModList *selectVoiceModList(ModStrip *ms) {
	if(!ms->voicePool || ms->voiceCount <= 0) {
		return NULL;
	}
	Voice *voice = NULL;
	for(int i = 0; i < ms->voiceCount; i++) {
		Voice *v = ms->voicePool[i];
		if(v && v->active) {
			if(!voice || v->samplesElapsed < voice->samplesElapsed) {
				voice = v;
			}
		}
	}
	if(!voice) {
		voice = ms->voicePool[0];
	}
	if(!voice || !voice->modList) {
		return NULL;
	}
	return voice->modList;
}

void drawModStrip(ModStrip *ms, Rectangle dest) {
	float t = GetTime();
	float tht_x = 1.8f * sinf(0.6f * t) + 1.2f * sinf(1.5f * t) + 0.8f * sinf(3.1f * t);
	float tht_y = 1.1f * sinf(17.5f * t) + 1.1f * sinf(1.8f * t) + 0.6f * sinf(3.3f * t);
	float dx = 3.2f * cosf(tht_x);
	float dy = 4.8f * sinf(tht_y);
	float clr_mod = sinf(6.6 * t);
	RenderTexture2D src = ms->pingActive ? ms->ping : ms->pong;
	RenderTexture2D tgt = ms->pingActive ? ms->pong : ms->ping;

	BeginTextureMode(tgt);
	ClearBackground(BLACK);
	DrawTexturePro(src.texture, (Rectangle){ 0, 0, ms->width, ms->height },
	               (Rectangle){ dx, dy, ms->width -dx, ms->height-dy },
	               (Vector2){ 0, 0 }, 0.0f, (Color){ fmaxf(249.0f + tht_x * 8.0f, 255.0f), fmaxf(249.0 + 8.0f * tht_y, 255.0f), fmax(255.0f + clr_mod * 5.0f, 255.0f), 255 });

	ModList *modList = selectVoiceModList(ms);
	if(modList && modList->count > 0) {
		int n = modList->count;
		float barW = ms->width / (float)n;
		for(int i = 0; i < n; i++) {
			Mod *m = modList->mods[i];
			if(!m || !m->output) {
				continue;
			}
			float val = getParameterValue(m->output);
			if(val < 0.0f) {
				val = 0.0f;
			}
			if(val > 1.0f) {
				val = 1.0f;
			}
			val = powf(val, MOD_STRIP_RESPONSE_CURVE);
			int x = (int)(i * barW) + 2;
			int bw = (int)barW - 4;
			if(bw < 1) {
				bw = 1;
			}
			int halfH = (int)(val * ((ms->height - 6) / 2.0f));
			int centerY = ms->height / 2;
			Color base_colour = modStripColor(m->type);
			base_colour.g = (int)(((float)base_colour.g/255.0f)*val*255.0f);
			base_colour.b *= 0.5f;
			base_colour.b += (int)((float)base_colour.b*val);
			base_colour.a = 250;
			DrawRectangle(x, centerY - halfH, bw, halfH, base_colour);
			DrawRectangle(x, centerY, bw, halfH, base_colour);
		}
	}
	EndTextureMode();

	ms->pingActive = !ms->pingActive;
	DrawTexturePro(tgt.texture, (Rectangle){ 0, 0, ms->width, ms->height },
	               dest, (Vector2){ 0, 0 }, 0.0f, WHITE);
}

void freeModStrip(ModStrip *ms) {
	UnloadRenderTexture(ms->ping);
	UnloadRenderTexture(ms->pong);
}

void initMixRing(MixRing *r) {
	for(int i = 0; i < MIX_RING_LEN; i++) {
		r->samples[i] = 0.0f;
	}
	r->writeIndex = 0;
	r->count = 0;
}

void pushMixRingSample(MixRing *r, float sample) {
	r->samples[r->writeIndex] = sample;
	r->writeIndex = (r->writeIndex + 1) % MIX_RING_LEN;
	if(r->count < MIX_RING_LEN) {
		r->count++;
	}
}

static float mixRingRead(const MixRing *r, int k) {
	return r->samples[(r->writeIndex + k) % MIX_RING_LEN];
}

static float mixRingClamped(const MixRing *r, int k) {
	float s = mixRingRead(r, k);
	if(s < -1.0f) {
		s = -1.0f;
	}
	if(s > 1.0f) {
		s = 1.0f;
	}
	return s;
}

void drawSampleWaveLines(const MixRing *r, Rectangle dest) {
	int w = (int)dest.width;
	int h = (int)dest.height;
	int centerY = dest.y + h / 2;
	Color col = getColourScheme()->vline;
	for(int x = 0; x < w; x++) {
		int k = (int)((long)x * MIX_RING_LEN / w);
		float s = mixRingClamped(r, k);
		int val = (int)(s * (h / 2));
		DrawLine(dest.x + x, centerY, dest.x + x, centerY + val, col);
	}
}

void drawSampleWavePolyline(const MixRing *r, Rectangle dest) {
	int h = (int)dest.height;
	int centerY = dest.y + h / 2;
	float xScale = dest.width / (float)(MIX_RING_LEN - 1);
	float yScale = h / 2.0f;
	Color col = getColourScheme()->poly;
	Vector2 prev = { dest.x, centerY + mixRingClamped(r, 0) * yScale };
	for(int i = 1; i < MIX_RING_LEN; i++) {
		Vector2 cur;
		cur.x = dest.x + i * xScale;
		cur.y = centerY + mixRingClamped(r, i) * yScale;
		DrawLineEx(prev, cur, 1.0f, col);
		prev = cur;
	}
}
