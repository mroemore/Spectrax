#include "viz.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef VIZ_INCLUDE_DIR
#define VIZ_INCLUDE_DIR "include"
#endif

static const char *tcc = "tcc";

#define VIZ_ERR_MAX 1024

static const char *ext_of(const char *path) {
	const char *dot = strrchr(path, '.');
	return dot ? dot + 1 : "";
}

static int ensure_dir(const char *dir) {
	struct stat st;
	if(stat(dir, &st) == 0) {
		return S_ISDIR(st.st_mode) ? 0 : -1;
	}
	return mkdir(dir, 0755);
}

static unsigned viz_hash(const char *s) {
	unsigned h = 2166136261u;
	while(*s) {
		h ^= (unsigned char)*s++;
		h *= 16777619u;
	}
	return h;
}

const char *viz_cache_dir(void) {
	const char *env = getenv("VIZULOBE_CACHE");
	if(env && env[0]) {
		return env;
	}
	static char dir[1024];
	static int inited = 0;
	if(!inited) {
		const char *xdg = getenv("XDG_CACHE_HOME");
		if(xdg && xdg[0]) {
			snprintf(dir, sizeof(dir), "%s/vizulobe", xdg);
		} else {
			const char *home = getenv("HOME");
			snprintf(dir, sizeof(dir), "%s/.cache/vizulobe", home ? home : ".");
		}
		inited = 1;
	}
	return dir;
}

static char *run_tcc(const char *src_path, char *out_so, size_t n_out, char *err_buf, size_t err_n) {
	char cmd[2048];
	snprintf(cmd, sizeof(cmd),
		"%s -shared -fPIC -o %s -I%s \"%s\" 2>&1",
		tcc, out_so, VIZ_INCLUDE_DIR, src_path);
	FILE *p = popen(cmd, "r");
	if(!p) {
		snprintf(err_buf, err_n, "popen failed for tcc");
		return err_buf;
	}
	char buf[512];
	size_t total = 0;
	err_buf[0] = '\0';
	while(fgets(buf, sizeof(buf), p)) {
		if(total + strlen(buf) < err_n) {
			strcat(err_buf, buf);
			total += strlen(buf);
		}
	}
	int rc = pclose(p);
	if(rc != 0) {
		if(err_buf[0] == '\0') {
			snprintf(err_buf, err_n, "tcc exited with code %d", rc);
		}
		return err_buf;
	}
	/* tcc emits shared objects without a PT_GNU_STACK segment, which the
	   kernel then assumes to require an executable stack and refuses to
	   dlopen on modern systems. patchelf clears that flag (no-op if the
	   .so already has a non-exec GNU_STACK). */
	{
		char cmd2[2048];
		snprintf(cmd2, sizeof(cmd2), "patchelf --clear-execstack \"%s\" 2>/dev/null", out_so);
		int _ = system(cmd2); (void)_;
	}
	return NULL;
}

static Viz *make_err(const char *path, const char *msg) {
	Viz *v = calloc(1, sizeof(Viz));
	if(!v) return NULL;
	v->kind = VIZ_KIND_ERR;
	if(path) snprintf(v->path, sizeof(v->path), "%s", path);
	snprintf(v->error, sizeof(v->error), "%s", msg);
	return v;
}

Viz *viz_load(const char *path) {
	if(!path || !path[0]) {
		return make_err(path ? path : "", "empty path");
	}
	const char *ext = ext_of(path);
	if(strcmp(ext, "c") != 0) {
		return make_err(path, "unsupported extension (expected .c)");
	}

	if(ensure_dir(viz_cache_dir()) != 0) {
		return make_err(path, "could not create cache dir");
	}
	char so_path[VIZ_PATH_MAX];
	snprintf(so_path, sizeof(so_path), "%s/viz_%08x.so", viz_cache_dir(), viz_hash(path));

	char err_buf[VIZ_ERR_MAX];
	char *err = run_tcc(path, so_path, sizeof(so_path), err_buf, sizeof(err_buf));
	if(err) {
		return make_err(path, err);
	}

	void *dl = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
	const char *dl_err = dlerror();
	if(!dl) {
		char m[VIZ_ERR_MAX];
		snprintf(m, sizeof(m), "dlopen failed: %s", dl_err ? dl_err : "?");
		return make_err(path, m);
	}

	viz_init_fn init = (viz_init_fn)dlsym(dl, "viz_init");
	const char *s_err = dlerror();
	if(!init) {
		char m[VIZ_ERR_MAX];
		snprintf(m, sizeof(m), "dlsym(viz_init) failed: %s", s_err ? s_err : "?");
		dlclose(dl);
		return make_err(path, m);
	}
	viz_frame_fn frame = (viz_frame_fn)dlsym(dl, "viz_frame");
	s_err = dlerror();
	if(!frame) {
		char m[VIZ_ERR_MAX];
		snprintf(m, sizeof(m), "dlsym(viz_frame) failed: %s", s_err ? s_err : "?");
		dlclose(dl);
		return make_err(path, m);
	}

	Viz *v = calloc(1, sizeof(Viz));
	if(!v) {
		dlclose(dl);
		return make_err(path, "out of memory");
	}
	v->kind = VIZ_KIND_C;
	snprintf(v->path, sizeof(v->path), "%s", path);
	v->u.c.dl = dl;
	v->u.c.init = init;
	v->u.c.frame = frame;
	memset(&v->u.c.ctx, 0, sizeof(v->u.c.ctx));
	return v;
}

const char *viz_error(const Viz *v) {
	return v ? v->error : "";
}

bool viz_is_loaded(const Viz *v) {
	if(!v) return false;
	if(v->kind == VIZ_KIND_C) {
		return v->u.c.dl && v->u.c.init && v->u.c.frame;
	}
	if(v->kind == VIZ_KIND_GLSL) {
		return v->u.gl.shader.id != 0;
	}
	return false;
}

void viz_free(Viz *v) {
	if(!v) return;
	if(v->kind == VIZ_KIND_C && v->u.c.dl) {
		dlclose(v->u.c.dl);
		v->u.c.dl = NULL;
	}
	free(v);
}
