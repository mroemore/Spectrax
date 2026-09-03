#include <stdio.h>
#include <string.h>
#include "scene.h"

void scene_init(Scene *s) {
	memset(s, 0, sizeof(*s));
	s->selected = -1;
}

int scene_add_fg(Scene *s, const char *path, int x, int y, int w, int h) {
	if(s->fg_count >= VIZ_MAX_FG) {
		return -1;
	}
	FgVizRect *r = &s->fg[s->fg_count];
	snprintf(r->path, sizeof(r->path), "%s", path);
	r->x = x;
	r->y = y;
	r->w = w;
	r->h = h;
	r->selected = 1;
	s->selected = s->fg_count;
	s->fg_count++;
	return s->selected;
}

bool scene_remove_fg(Scene *s, int idx) {
	if(idx < 0 || idx >= s->fg_count) {
		return false;
	}
	for(int i = idx; i < s->fg_count - 1; i++) {
		s->fg[i] = s->fg[i + 1];
	}
	s->fg_count--;
	if(s->selected == idx) {
		s->selected = -1;
	} else if(s->selected > idx) {
		s->selected--;
	}
	return true;
}

void scene_set_bg(Scene *s, const char *path) {
	snprintf(s->bg_path, sizeof(s->bg_path), "%s", path);
}

int scene_hit_test(Scene *s, int mx, int my) {
	for(int i = s->fg_count - 1; i >= 0; i--) {
		const FgVizRect *r = &s->fg[i];
		if(mx >= r->x && mx < r->x + r->w && my >= r->y && my < r->y + r->h) {
			return i;
		}
	}
	return -1;
}

void scene_select(Scene *s, int idx) {
	if(idx >= 0 && idx < s->fg_count) {
		for(int i = 0; i < s->fg_count; i++) {
			s->fg[i].selected = (i == idx);
		}
		s->selected = idx;
	} else {
		for(int i = 0; i < s->fg_count; i++) {
			s->fg[i].selected = 0;
		}
		s->selected = -1;
	}
}

void scene_drag(Scene *s, int dx, int dy) {
	if(s->selected < 0 || s->selected >= s->fg_count) {
		return;
	}
	s->fg[s->selected].x += dx;
	s->fg[s->selected].y += dy;
}

void scene_resize(Scene *s, int dw, int dh) {
	if(s->selected < 0 || s->selected >= s->fg_count) {
		return;
	}
	FgVizRect *r = &s->fg[s->selected];
	r->w += dw;
	r->h += dh;
	if(r->w < VIZ_MIN_RECT) {
		r->w = VIZ_MIN_RECT;
	}
	if(r->h < VIZ_MIN_RECT) {
		r->h = VIZ_MIN_RECT;
	}
}