/*
 * spectrax_bench — single-binary stress test for Spectrax subsystems.
 *
 * Dispatches --bench={audio,gfx,fileio,all} to the per-mode subtests and
 * writes a JSON+CSV report per run.
 *
 * The bench is device-aware: --device resolves a device profile on the HOST
 * (the profile is a build/deploy artefact, the device does not carry the
 * TOML), and the resolved panel size, GPU API and data directory are
 * stamped into the report so an on-device run is still attributable.
 */
#include "bench_audio.h"
#include "bench_fileio.h"
#include "bench_gfx.h"
#include "bench_options.h"
#include "bench_report.h"
#include "bench_xvfb.h"

#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>

volatile sig_atomic_t bench_stop_requested = 0;

static void bench_signal_handler(int sig) {
    (void)sig;
    bench_stop_requested = 1;
}

static void usage(void) {
    fputs(
"Usage: spectrax_bench --bench={audio|gfx|fileio|all} [options]\n"
"\n"
"Options:\n"
"  --bench=MODE      audio | gfx | fileio | all\n"
"  --out=DIR         output dir (default: ./runs/<UTC>)\n"
"  --device=ID       device profile id (stamps the report, picks the display)\n"
"  --display=WxH     gfx window size (default: the profile's panel size)\n"
"  --workdir=DIR     fileio working directory (default: the profile's data dir)\n"
"  --gpu=API         gles2 | gles3 (default: the profile's gpu.api)\n"
"  --duration=SECS   audio duration in seconds (default 10)\n"
"  --voices=N        voices per channel for audio mode (default 4)\n"
"  --channels=N      channels for audio mode (default 4)\n"
"  --no-neon         disable NEON vectorization in audio hot path\n"
"  --seed=N          deterministic seed (default 0xC0FFEE)\n"
"  --help            this help\n",
    stdout);
}

static void default_out_dir(char *buf, size_t n) {
    time_t now = time(NULL);
    struct tm gm;
    gmtime_r(&now, &gm);
    strftime(buf, n, "./runs/%Y%m%dT%H%M%SZ", &gm);
}

/* --- device profile resolution (host side) ------------------------------- */

static int extract_toml_string(const char *text, const char *key, char *out,
                               size_t out_sz);

static int dir_is_repo(const char *d) {
    char p[1024];
    snprintf(p, sizeof p, "%s/scripts/lib/profile.py", d);
    if (access(p, R_OK) != 0) return 0;
    snprintf(p, sizeof p, "%s/devices", d);
    return access(p, R_OK) == 0;
}

static int bench_find_repo_root(char *out, size_t n) {
    char cand[1024];
    if (getcwd(cand, sizeof cand)) {
        char d[1024];
        snprintf(d, sizeof d, "%s", cand);
        while (strcmp(d, "/") != 0) {
            if (dir_is_repo(d)) { snprintf(out, n, "%s", d); return 0; }
            char *slash = strrchr(d, '/');
            if (!slash) break;
            *slash = '\0';
        }
    }
    char exe[1024];
    ssize_t len = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (len > 0) {
        exe[len] = '\0';
        char *slash = strrchr(exe, '/');
        while (slash) {
            *slash = '\0';
            if (dir_is_repo(exe)) { snprintf(out, n, "%s", exe); return 0; }
            slash = strrchr(exe, '/');
        }
    }
    return -1;
}

/* Reads device.toml next to the running binary -- the fragment the deploy
   writes. This is the on-device profile source: the TOML profile itself is
   a build/deploy artefact and is not deployed. Returns 0 on success. */
static int bench_read_device_fragment(const char *device, int *w, int *h,
                                      char *api, size_t api_sz,
                                      char *data_dir, size_t dir_sz) {
    char exe[1024];
    ssize_t len = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (len <= 0) return -1;
    exe[len] = '\0';
    char *slash = strrchr(exe, '/');
    if (!slash) return -1;
    *slash = '\0';
    char path[1100];
    /* The bench lives in src/tools/bench in the build tree and beside its
       siblings in the deployed tree; check "." and "..". */
    const char *dirs[] = {".", "..", NULL};
    FILE *f = NULL;
    for (int i = 0; dirs[i]; i++) {
        snprintf(path, sizeof path, "%s/%s/device.toml", exe, dirs[i]);
        f = fopen(path, "r");
        if (f) break;
    }
    if (!f) return -1;
    static char buf[4096];
    size_t total = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[total] = '\0';

    char val[1024];
    if (extract_toml_string(buf, "data_dir", val, sizeof val) == 0) {
        snprintf(data_dir, dir_sz, "%s", val);
    }
    if (extract_toml_string(buf, "gpu_driver", val, sizeof val) == 0) {
        /* panfrost -> gles3 on this SoC; libmali would be gles2. */
        snprintf(api, api_sz, "%s", strcmp(val, "panfrost") == 0 ? "gles3" : "gles2");
    }
    (void)device; (void)w; (void)h;
    return 0;
}

/* Copies the value of KEY = "..." out of a TOML fragment. */
static int extract_toml_string(const char *text, const char *key, char *out,
                               size_t out_sz) {
    char pat[128];
    snprintf(pat, sizeof pat, "%s = \"", key);
    const char *p = strstr(text, pat);
    if (!p) return -1;
    p += strlen(pat);
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < out_sz) out[i++] = *p++;
    out[i] = '\0';
    return 0;
}

/* Copies the value of KEY='...' out of the loader's output. */
static int extract_value(const char *text, const char *key, char *out, size_t out_sz) {
    char pat[128];
    snprintf(pat, sizeof pat, "%s='", key);
    const char *p = strstr(text, pat);
    if (!p) return -1;
    p += strlen(pat);
    size_t i = 0;
    while (*p && *p != '\'' && i + 1 < out_sz) {
        if (*p == '\\' && p[1] == '\'') { p++; }
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 0;
}

/* Resolves the device profile on the HOST, not on the device: the profile
   is a build/deploy artefact and the device does not carry the TOML. The
   host stamps the resolved values into the report so on-device runs are
   still attributable. Returns 0 when no profile could be resolved. */
static int bench_resolve_profile(const char *device, int *w, int *h,
                                  char *api, size_t api_sz, char *data_dir,
                                  size_t dir_sz) {
    char repo[1024];
    if (bench_find_repo_root(repo, sizeof repo) != 0) {
        /* Not a checkout -- this is a deployed device build. Fall back to
         * the fragment the deploy writes next to the binaries, which
         * carries the same paths the profile would have produced. Without
         * this the on-device bench reports device_id "host" and measures
         * /tmp (a tmpfs, i.e. RAM) instead of the SD card. */
        return bench_read_device_fragment(device, w, h, api, api_sz,
                                          data_dir, dir_sz);
    }
    char cmd[2048];
    snprintf(cmd, sizeof cmd, "cd '%s' && scripts/lib/profile.py --shell '%s' 2>/dev/null",
             repo, device);
    FILE *f = popen(cmd, "r");
    if (!f) return -1;
    static char buf[16384];
    size_t total = 0;
    size_t n;
    while (total + 1 < sizeof buf && (n = fread(buf + total, 1, sizeof buf - total - 1, f)) > 0) {
        total += n;
    }
    buf[total] = '\0';
    int rc = pclose(f);
    if (rc != 0 || total == 0) return -1;

    char val[1024];
    if (extract_value(buf, "DISPLAY_WIDTH", val, sizeof val) == 0) *w = atoi(val);
    if (extract_value(buf, "DISPLAY_HEIGHT", val, sizeof val) == 0) *h = atoi(val);
    if (extract_value(buf, "GPU_API", val, sizeof val) == 0) snprintf(api, api_sz, "%s", val);
    if (extract_value(buf, "PATHS_DATA_DIR", val, sizeof val) == 0) {
        snprintf(data_dir, dir_sz, "%s", val);
    }
    return 0;
}

/* --- main ---------------------------------------------------------------- */

int main(int argc, char **argv) {
    BenchOptions opts;
    memset(&opts, 0, sizeof opts);
    opts.width = 640;
    opts.height = 480;
    opts.duration = 10;
    opts.voices = 4;
    opts.channels = 4;
    opts.seed = 0xC0FFEE;
    int help = 0;
    int display_w = 0, display_h = 0;

    static struct option longopts[] = {
        {"bench", required_argument, 0, 'b'},
        {"out", required_argument, 0, 'o'},
        {"device", required_argument, 0, 'D'},
        {"display", required_argument, 0, 'W'},
        {"workdir", required_argument, 0, 'K'},
        {"gpu", required_argument, 0, 'G'},
        {"duration", required_argument, 0, 'd'},
        {"voices", required_argument, 0, 'v'},
        {"channels", required_argument, 0, 'c'},
        {"no-neon", no_argument, 0, 'N'},
        {"seed", required_argument, 0, 's'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0},
    };

    int ch;
    while ((ch = getopt_long(argc, argv, "b:o:D:W:K:G:d:v:c:s:hN", longopts, NULL)) != -1) {
        switch (ch) {
            case 'b': opts.bench = optarg; break;
            case 'o': opts.out = optarg; break;
            case 'D': opts.device = optarg; break;
            case 'W': {
                const char *p = optarg;
                display_w = atoi(p);
                const char *x = strchr(p, 'x');
                display_h = x ? atoi(x + 1) : 0;
                break;
            }
            case 'K': opts.workdir = optarg; break;
            case 'G': opts.gpu = optarg; break;
            case 'd': opts.duration = atoi(optarg); break;
            case 'v': opts.voices = atoi(optarg); break;
            case 'c': opts.channels = atoi(optarg); break;
            case 'N': opts.no_neon = 1; break;
            case 's': opts.seed = (unsigned)strtoul(optarg, NULL, 0); break;
            case 'h': help = 1; break;
            default: usage(); return 2;
        }
    }

    if (help) { usage(); return 0; }
    if (!opts.bench) { usage(); return 2; }

    char profile_api[32] = "gles2";
    char profile_dir[1024] = "";
    char device_label[128] = "host";
    if (opts.device) {
        int pw = 0, ph = 0;
        char api[32];
        char dir[1024];
        api[0] = '\0';
        dir[0] = '\0';
        if (bench_resolve_profile(opts.device, &pw, &ph, api, sizeof api,
                                  dir, sizeof dir) == 0) {
            if (pw > 0) opts.width = pw;
            if (ph > 0) opts.height = ph;
            if (api[0]) snprintf(profile_api, sizeof profile_api, "%s", api);
            if (dir[0]) snprintf(profile_dir, sizeof profile_dir, "%s", dir);
            snprintf(device_label, sizeof device_label, "%s", opts.device);
        } else {
            fprintf(stderr, "warning: could not resolve the device profile '%s'; "
                            "using the host defaults\n", opts.device);
        }
    }
    if (display_w > 0) opts.width = display_w;
    if (display_h > 0) opts.height = display_h;
    const char *gpu_api = opts.gpu ? opts.gpu : profile_api;
    /* On a device the fileio numbers must describe the real storage volume,
     * not a tmpfs, so the profile's data dir is the default. */
    const char *workdir = opts.workdir ? opts.workdir
                                       : (profile_dir[0] ? profile_dir : NULL);
    opts.workdir = workdir;

    char out_buf[256];
    if (!opts.out) { default_out_dir(out_buf, sizeof out_buf); opts.out = out_buf; }

    signal(SIGINT,  bench_signal_handler);
    signal(SIGTERM, bench_signal_handler);
    signal(SIGHUP,  bench_signal_handler);

    BenchReport *r = bench_report_new(opts.out);
    bench_report_str(r, "meta", "bench", opts.bench);
    bench_report_str(r, "meta", "device", device_label);
    bench_report_str(r, "meta", "device_id", opts.device ? opts.device : "host");
    bench_report_metric(r, "meta", "display_width", opts.width);
    bench_report_metric(r, "meta", "display_height", opts.height);
    bench_report_str(r, "meta", "gpu_api", gpu_api);
    bench_report_metric(r, "meta", "duration_s", opts.duration);
    bench_report_metric(r, "meta", "voices", opts.voices);
    bench_report_metric(r, "meta", "channels", opts.channels);
    bench_report_metric(r, "meta", "no_neon", opts.no_neon);
    bench_report_metric(r, "meta", "seed", opts.seed);
    bench_report_str(r, "meta", "workdir", workdir ? workdir : "(tmp)");

    int rc = 0;
    int headless = opts.device ? 1 : 0;
    pid_t xvfb = 0;

    if (strcmp(opts.bench, "audio") == 0) {
        rc = bench_audio_run(&opts, r);
    } else if (strcmp(opts.bench, "fileio") == 0) {
        rc = bench_fileio_run(&opts, r);
    } else if (strcmp(opts.bench, "gfx") == 0) {
        xvfb = bench_xvfb_start_if_needed(headless, opts.width, opts.height);
        if (xvfb < 0) rc = 1;
        else rc = bench_gfx_run(&opts, r);
        if (xvfb > 0) { kill(xvfb, SIGTERM); waitpid(xvfb, NULL, 0); }
    } else if (strcmp(opts.bench, "all") == 0) {
        rc = bench_audio_run(&opts, r);
        if (rc == 0) {
            xvfb = bench_xvfb_start_if_needed(headless, opts.width, opts.height);
            if (xvfb < 0) rc = 1;
            else rc = bench_gfx_run(&opts, r);
            if (xvfb > 0) { kill(xvfb, SIGTERM); waitpid(xvfb, NULL, 0); }
        }
        if (rc == 0) rc = bench_fileio_run(&opts, r);
    } else {
        bench_report_log(r, "error: unknown --bench=%s\n", opts.bench);
        rc = 2;
    }

    if (bench_stop_requested) {
        bench_report_log(r, "bench: interrupted (signal); flushing the partial report\n");
    }
    bench_report_flush(r);
    bench_report_free(r);
    bench_report_log(NULL, "report: %s\n", opts.out);
    return rc;
}
