#include "viz.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef VIZ_INCLUDE_DIR
#define VIZ_INCLUDE_DIR "include"
#endif

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

static int run_cmd_argv(char *const argv[], char *err_buf, size_t err_n) {
	int fds[2];
	if(pipe(fds) != 0) {
		snprintf(err_buf, err_n, "pipe failed");
		return -1;
	}
	pid_t pid = fork();
	if(pid < 0) {
		snprintf(err_buf, err_n, "fork failed");
		close(fds[0]);
		close(fds[1]);
		return -1;
	}
	if(pid == 0) {
		close(fds[0]);
		dup2(fds[1], 1);
		dup2(fds[1], 2);
		close(fds[1]);
		execvp(argv[0], argv);
		_exit(127);
	}
	close(fds[1]);
	size_t total = 0;
	ssize_t n;
	char buf[256];
	while((n = read(fds[0], buf, sizeof(buf))) > 0) {
		if(total + (size_t)n < err_n) {
			memcpy(err_buf + total, buf, (size_t)n);
			total += (size_t)n;
		}
	}
	close(fds[0]);
	err_buf[total] = '\0';
	int status = 0;
	waitpid(pid, &status, 0);
	if(WIFEXITED(status)) {
		return WEXITSTATUS(status);
	}
	return -1;
}

static char *run_tcc(const char *src_path, char *out_so, size_t n_out, char *err_buf, size_t err_n) {
	(void)n_out;
	char *const tcc_argv[] = {
		"tcc", "-shared", "-fPIC", "-o", out_so,
		"-I", VIZ_INCLUDE_DIR,
		(char *)src_path,
		NULL
	};
	err_buf[0] = '\0';
	int rc = run_cmd_argv(tcc_argv, err_buf, err_n);
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
	char *const patch_argv[] = { "patchelf", "--clear-execstack", out_so, NULL };
	char dummy[64];
	run_cmd_argv(patch_argv, dummy, sizeof(dummy));
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

static char *viz_read_file(const char *path, size_t *out_len) {
	FILE *f = fopen(path, "rb");
	if(!f) {
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if(size < 0) {
		fclose(f);
		return NULL;
	}
	char *buf = (char *)malloc((size_t)size + 1);
	if(!buf) {
		fclose(f);
		return NULL;
	}
	size_t n = fread(buf, 1, (size_t)size, f);
	buf[n] = '\0';
	fclose(f);
	if(out_len) {
		*out_len = n;
	}
	return buf;
}

static Viz *viz_load_c(const char *path);
static Viz *viz_load_glsl(const char *path);

static Viz *viz_load_glsl(const char *path) {
	char *src = viz_read_file(path, NULL);
	if(!src) {
		return make_err(path, "could not read file");
	}
	Shader shader = LoadShaderFromMemory(NULL, src);
	free(src);
	if(shader.id == 0) {
		return make_err(path, "shader compile failed (see raylib log)");
	}
	Viz *v = calloc(1, sizeof(Viz));
	v->kind = VIZ_KIND_GLSL;
	snprintf(v->path, sizeof(v->path), "%s", path);
	v->u.gl.shader = shader;
	v->u.gl.loc_time = GetShaderLocation(shader, "uTime");
	v->u.gl.loc_dt = GetShaderLocation(shader, "uDt");
	v->u.gl.loc_resolution = GetShaderLocation(shader, "uResolution");
	v->u.gl.loc_waveform = GetShaderLocation(shader, "uWaveform");
	v->u.gl.loc_spectrum = GetShaderLocation(shader, "uSpectrum");
	v->u.gl.loc_audio = GetShaderLocation(shader, "uAudio");
	v->u.gl.loc_backbuffer = GetShaderLocation(shader, "uBackbuffer");
	return v;
}

Viz *viz_load(const char *path) {
	if(!path || !path[0]) {
		return make_err(path ? path : "", "empty path");
	}
	const char *ext = ext_of(path);
	if(strcmp(ext, "c") == 0) {
		return viz_load_c(path);
	}
	if(strcmp(ext, "frag") == 0) {
		return viz_load_glsl(path);
	}
	return make_err(path, "unsupported extension (expected .c or .frag)");
}

static Viz *viz_load_c(const char *path) {
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
	} else if(v->kind == VIZ_KIND_GLSL) {
		UnloadShader(v->u.gl.shader);
	}
	free(v);
}
