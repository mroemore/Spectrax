# Section 1 — Foundation & Build Pipeline

**Status:** ✅ **complete**

## Outcome

Clean fil-c build of a stub `dsp_main.c` that renders 1 second of silence
to a 44.1 kHz mono PCM WAV. Tests pass. Build system (`make filc`,
`make filc-test`, `make filc-clean`) integrated alongside the existing
targets without breaking them.

## Files added

| File | Purpose | LOC |
|------|---------|-----|
| `dsp/wav_writer.h` | Public API | ~25 |
| `dsp/wav_writer.c` | PCM 16-bit mono writer | ~80 |
| `dsp/dsp_main.c` | Stub entry point | ~30 |
| `dsp/test_wav_writer.c` | Header + sample roundtrip tests | ~85 |

## Files modified

| File | Change |
|------|--------|
| `Makefile` | Added `filc`, `filc-test`, `filc-clean` targets; new vars `FILC_CC`, `FILC_CFLAGS`, `FILC_DSP_DIR` etc. |

## Build & run results

```sh
$ make filc
/usr/local/bin/clang -c dsp/dsp_main.c -o dsp/dsp_main.o -O2 -g -Iinclude
/usr/local/bin/clang -c dsp/wav_writer.c -o dsp/wav_writer.o -O2 -g -Iinclude
/usr/local/bin/clang -o bin/spectrax_filc dsp/dsp_main.o dsp/wav_writer.o -O2 -g -Iinclude

$ ./bin/spectrax_filc
wrote 44100 samples of silence to out.wav

$ file out.wav
out.wav: RIFF (little-endian) data, WAVE audio, Microsoft PCM, 16 bit, mono 44100 Hz

$ stat -c '%s bytes' out.wav
88244 bytes   # exactly 44 + 44100*2, as expected

$ ./bin/test_wav_writer
PASS test_empty_wav
PASS test_with_samples
```

Binary uses fil-c's loader:
```
INTERP → /opt/fil-c/.../pizfix/lib/ld-yolo-x86_64.so
NEEDED → libc.so, libpizlo.so, libyoloc.so
```

## Fil-c gotchas hit (and resolutions)

| Gotcha | Resolution |
|--------|------------|
| `ar = clang` in meson native file — clang can't act as archiver | N/A here (Makefile, no static libs in this section). Documented in master. |
| Default `-Wall` warnings | None triggered. CFLAGS stayed minimal. |
| `write(2)` on stdout | Works as expected — fil-c intercepts and checks buffer bounds. |
| `fopen/fwrite/fclose` | All work, return values checked. |

## Tests added

| Test | Verifies |
|------|----------|
| `test_empty_wav` | Header bytes, RIFF/WAVE/fmt /data magic, file size for 0 samples |
| `test_with_samples` | Sample roundtrip, data subchunk size = N*2 |

## Decisions made in this section

1. **dsp/ subdirectory** — kept separate from `src/` to make it obvious
   that this is the fil-c-only build path. `src/` remains untouched for
   the gcc build until we wire it up in later sections.
2. **WAV format hardcoded** to mono 16-bit 44.1 kHz. No API for
   configurability yet — adds nothing for now and complicates the surface.
3. **No make `all` change** — default `all` target still builds the
   raylib/portaudio `spectrax` binary via gcc. `make filc` is opt-in.

## Section exit criteria — all met

- [x] fil-c build system added (`make filc`)
- [x] Builds with zero warnings
- [x] Binary runs and produces valid WAV (verified via `file(1)`)
- [x] Tests pass under fil-c
- [x] Section report updated