#include <math.h>

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

void collapseBufferToColumn(const float *samples, int bufferSize, unsigned char *lo, unsigned char *hi, int columnHeight) {
	for(int r = 0; r < columnHeight; r++) {
		int start = (int)((float)r * bufferSize / columnHeight);
		int end = (int)((float)(r + 1) * bufferSize / columnHeight);
		if(end <= start) {
			end = start + 1;
		}
		if(end > bufferSize) {
			end = bufferSize;
		}
		int loRow = sampleToRow(samples[start]);
		int hiRow = loRow;
		for(int i = start; i < end; i++) {
			int y = sampleToRow(samples[i]);
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
		out[r] = (Color){ 0, 0, 0, 255 };
	}
	for(int r = 0; r < columnHeight; r++) {
		for(int y = lo[r]; y <= hi[r]; y++) {
			out[columnHeight - 1 - y] = waveColour;
		}
	}
}

void initBufferScroller(BufferScroller *bs) {
	bs->image = GenImageColor(SCROLLER_WIDTH, SCROLLER_COLUMN_HEIGHT, (Color){ 0, 0, 0, 255 });
	bs->texture = LoadTextureFromImage(bs->image);
	SetTextureFilter(bs->texture, TEXTURE_FILTER_POINT);
	bs->writeColumn = 0;
	bs->pendingHead = 0;
	bs->pendingTail = 0;
	bs->pendingCount = 0;
	bs->framePos = 0;
}

void pushBufferScrollerFrame(BufferScroller *bs, float sample) {
	bs->staging[bs->framePos] = sample;
	bs->framePos++;
	if(bs->framePos >= SCROLLER_BUFFER_SIZE) {
		bs->framePos = 0;
		collapseBufferToColumn(bs->staging, SCROLLER_BUFFER_SIZE,
		                       bs->pendingLo[bs->pendingTail],
		                       bs->pendingHi[bs->pendingTail],
		                       SCROLLER_COLUMN_HEIGHT);
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
	const Color waveColour = (Color){ 60, 255, 150, 255 };
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
			return (Color){ 0, 255, 255, 255 };
		case MT_ENV:
			return (Color){ 130, 255, 130, 255 };
		case MT_RND:
			return (Color){ 255, 80, 255, 255 };
		case MT_OFS:
			return (Color){ 190, 190, 190, 255 };
		default:
			return (Color){ 210, 210, 210, 255 };
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
	float theta = 1.8f * sinf(0.6f * t) + 1.2f * sinf(1.5f * t) + 0.8f * sinf(3.1f * t);
	float dx = 1.8f * cosf(theta);
	float dy = 1.8f * sinf(theta);

	RenderTexture2D src = ms->pingActive ? ms->ping : ms->pong;
	RenderTexture2D tgt = ms->pingActive ? ms->pong : ms->ping;

	BeginTextureMode(tgt);
	ClearBackground(BLACK);
	DrawTexturePro(src.texture, (Rectangle){ 0, 0, ms->width, ms->height },
	               (Rectangle){ dx, dy, ms->width, ms->height },
	               (Vector2){ 0, 0 }, 0.0f, (Color){ 246, 246, 246, 255 });

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
			int x = (int)(i * barW) + 2;
			int bw = (int)barW - 4;
			if(bw < 1) {
				bw = 1;
			}
			int halfH = (int)(val * ((ms->height - 6) / 2.0f));
			DrawRectangle(x, 3, bw, halfH, modStripColor(m->type));
			DrawRectangle(x, ms->height - 3 - halfH, bw, halfH, modStripColor(m->type));
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