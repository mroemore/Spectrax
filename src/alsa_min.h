#ifndef ALSA_MIN_H
#define ALSA_MIN_H

/* The slice of libasound's API the device output backend uses.
 *
 * A host build includes the real <alsa/asoundlib.h>, so the declarations here
 * are checked against it. A device build cannot: the aarch64 cross sysroot
 * carries no ALSA headers (the project links libasound from a link-time stub
 * instead, see scripts/build-target-libs.sh), and pulling the host's C library
 * headers into a target compile is exactly the header mixing that breaks
 * struct layouts. The subset below is ABI-stable -- opaque handles, fixed-width
 * frame counts and the four enum values the backend passes -- so declaring it
 * keeps the cross build hermetic without vendoring alsa-lib. */

#if defined(__has_include)
#if __has_include(<alsa/asoundlib.h>)
#include <alsa/asoundlib.h>
#define ALSA_MIN_HAVE_SYSTEM_HEADER 1
#endif
#endif

#ifndef ALSA_MIN_HAVE_SYSTEM_HEADER

typedef struct _snd_pcm snd_pcm_t;
typedef struct _snd_pcm_hw_params snd_pcm_hw_params_t;
typedef unsigned long snd_pcm_uframes_t;
typedef long snd_pcm_sframes_t;
/* enum snd_pcm_stream_t / _access_t / _format_t are int-sized on every ABI. */
typedef int snd_pcm_stream_t;
typedef int snd_pcm_access_t;
typedef int snd_pcm_format_t;
typedef void (*snd_lib_error_handler_t)(const char *file, int line, const char *function,
                                        int err, const char *fmt, ...);

#define SND_PCM_STREAM_PLAYBACK 0
#define SND_PCM_ACCESS_RW_INTERLEAVED 3
#define SND_PCM_FORMAT_S16_LE 2
#define SND_PCM_FORMAT_S32_LE 10

int snd_pcm_open(snd_pcm_t **pcm, const char *name, snd_pcm_stream_t stream, int mode);
int snd_pcm_close(snd_pcm_t *pcm);
int snd_pcm_prepare(snd_pcm_t *pcm);
int snd_pcm_drop(snd_pcm_t *pcm);
snd_pcm_sframes_t snd_pcm_writei(snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size);
int snd_pcm_wait(snd_pcm_t *pcm, int timeout);
int snd_pcm_resume(snd_pcm_t *pcm);
int snd_pcm_recover(snd_pcm_t *pcm, int err, int silent);
int snd_pcm_hw_params_malloc(snd_pcm_hw_params_t **ptr);
void snd_pcm_hw_params_free(snd_pcm_hw_params_t *obj);
int snd_pcm_hw_params_any(snd_pcm_t *pcm, snd_pcm_hw_params_t *params);
int snd_pcm_hw_params_current(snd_pcm_t *pcm, snd_pcm_hw_params_t *params);
int snd_pcm_hw_params(snd_pcm_t *pcm, snd_pcm_hw_params_t *params);
int snd_pcm_hw_params_set_access(snd_pcm_t *pcm, snd_pcm_hw_params_t *params,
                                 snd_pcm_access_t access);
int snd_pcm_hw_params_test_format(snd_pcm_t *pcm, snd_pcm_hw_params_t *params,
                                  snd_pcm_format_t val);
int snd_pcm_hw_params_set_format(snd_pcm_t *pcm, snd_pcm_hw_params_t *params,
                                 snd_pcm_format_t val);
int snd_pcm_hw_params_set_channels(snd_pcm_t *pcm, snd_pcm_hw_params_t *params,
                                   unsigned int val);
int snd_pcm_hw_params_get_channels(const snd_pcm_hw_params_t *params, unsigned int *val);
int snd_pcm_hw_params_set_rate(snd_pcm_t *pcm, snd_pcm_hw_params_t *params,
                               unsigned int val, int dir);
int snd_pcm_hw_params_get_rate(const snd_pcm_hw_params_t *params, unsigned int *val,
                               int *dir);
int snd_pcm_hw_params_set_period_size_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *params,
                                           snd_pcm_uframes_t *val, int *dir);
int snd_pcm_hw_params_set_buffer_size_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *params,
                                           snd_pcm_uframes_t *val);
int snd_pcm_hw_params_set_period_size(snd_pcm_t *pcm, snd_pcm_hw_params_t *params,
                                      snd_pcm_uframes_t val, int dir);
int snd_pcm_hw_params_set_buffer_size(snd_pcm_t *pcm, snd_pcm_hw_params_t *params,
                                      snd_pcm_uframes_t val);
int snd_pcm_hw_params_get_period_size(const snd_pcm_hw_params_t *params,
                                      snd_pcm_uframes_t *frames, int *dir);
int snd_pcm_hw_params_get_buffer_size(const snd_pcm_hw_params_t *params,
                                      snd_pcm_uframes_t *val);
const char *snd_strerror(int errnum);
const char *snd_pcm_format_name(const snd_pcm_format_t format);
int snd_lib_error_set_handler(snd_lib_error_handler_t handler);

#endif /* ALSA_MIN_HAVE_SYSTEM_HEADER */

#endif /* ALSA_MIN_H */
