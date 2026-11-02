# RG35XX SP runbook

Everything here is host-side tooling from the repository root. The device
steps need the hardware; the host steps do not.

## Before the first deploy
1. Flash ROCKNIX (H700 aarch64) and copy the right DTB
   (`sun50i-h700-anbernic-rg35xx-sp.dtb`, or the `-v2-panel` variant) to the
   root of the ROCKNIX partition, renamed `dtb.img`. The two are not
   interchangeable: the wrong one gives a garbled screen.
2. Boot. In EmulationStation: Main Menu -> Network Settings -> Enable Network,
   pick the SSID, enter the key.
3. Main Menu -> Network Settings -> Network Services -> Enable SSH.
4. Main Menu -> System Settings -> set a **root password** (ROCKNIX regenerates
   it on every boot otherwise).
5. USB-C: if the transport is `usb-gadget`, Main Menu -> Network Settings ->
   gadget mode -> network.

## Probing the device
    scripts/probe-os.sh      --device rg35xxsp --ip <ip>
    scripts/probe-paths.sh   --device rg35xxsp --ip <ip>
    scripts/probe-usb.sh     --device rg35xxsp --ip <ip> --apply
    scripts/probe-display.sh --device rg35xxsp --ip <ip>
    scripts/probe-audio.sh   --device rg35xxsp --ip <ip>
    scripts/probe-cpu.sh     --device rg35xxsp --ip <ip>
    scripts/probe-mem.sh     --device rg35xxsp --ip <ip>
    scripts/probe-lid.sh     --device rg35xxsp --ip <ip>
    scripts/probe-storage.sh --device rg35xxsp --ip <ip>
    scripts/probe-gpu.sh     --device rg35xxsp --ip <ip>

`--apply` writes the findings into `~/.config/spectrax/devices/rg35xxsp.toml`
and drops the resolved key from the profile's `[probes] needed` list. A probe
never fails the build: it reports what it found and exits 0.

### Status on a real unit (ROCKNIX 20261001, 2026-10-22)

Resolved: `userspace_arch` (aarch64), `ports_dir`
(`/storage/roms/ports`), `display_mode` (640x480 native), `audio_device`
(`dmix` at 48000).

Still open, and why:
- `usb_gadget` -- needs the USB-C port exercised with a host on the other
  end, which this session had no cable for.
- `cpu_governor`, `lid_switch`, `ram_type` -- not yet probed. Note the RAM
  dispute is unresolved: Anbernic say DDR3, the ROCKNIX wiki says LPDDR4.

### Audio on this device

The H700 exposes no 44100 Hz device at all: `hw:0,0` is 192000 (no plug
layer), `sysdefault` reports `INT_MAX`, and only `dmix` is usable, at
48000. The engine therefore runs at whatever rate the opened stream
reports rather than a compile-time 44100, and `openAudioStream()` prefers
the plugin devices. Two upstream PortAudio assumptions break on this
stack and are patched by `scripts/build-target-libs.sh` (see the comments
there): `assert(maxPeriods > 1)`, and trusting period-size bounds that an
unconstrained plugin PCM reports as `min = max = 0`.

Observed on the unit: `device=dmix buffer=256 rate=48000`, stream state
`RUNNING`, and the `bench_audio` DSP loop at 0 xruns with a real-time
ratio of ~0.91 for 16 voices.

## First-time host setup
    scripts/setup-device.sh --device rg35xxsp

This discovers the device, installs the host SSH key and records the address
in the host-local profile override. The RG35XX SP's `cfw.password_source` is
`user-set`, so set a root password on the device first (step 4 above) and
the key install then runs unattended.

## Building
    scripts/gen-cross-file.sh rg35xxsp
    meson setup build-rg35xxsp --cross-file build-rg35xxsp/cross.ini --buildtype=release
    meson compile -C build-rg35xxsp spectrax spectrax_bench instrument_harness

The binaries land in `build-rg35xxsp/src/...` (meson puts an executable next
to the meson.build that declares it). Do **not** run `meson install` for a
cross build: `bin/` holds the host build. The install script refuses to
touch it for a cross target.

The aarch64 library bundle (`lib/aarch64/`, `include/aarch64/`) is committed.
To rebuild it:

    scripts/build-target-libs.sh aarch64

That needs the host's EGL/GLES2/GBM/DRM/ALSA development headers (they are
only used for their headers and symbol lists) and the aarch64 cross
toolchain.

## Deploying
    scripts/deploy-spectrax.sh --device rg35xxsp            # auto transport
    scripts/deploy-spectrax.sh --device rg35xxsp --launch --out=runs/

The transport ladder is `usb-gadget` -> `wifi-ssh` -> `sd`; the script prints
the one it used. The USB gadget uses the fixed link-local pair
169.254.170.1 (host) / 169.254.170.2 (device), so no discovery is needed for
it:

    scripts/usb-gadget.sh --device rg35xxsp          # report the link state
    scripts/usb-gadget.sh --device rg35xxsp --mode network

The SD fallback needs the card mounted on the host:

    scripts/deploy-sd.sh /media/sdcard --device rg35xxsp
    scripts/inspect-sd.sh --dir /media/sdcard --device rg35xxsp

## Benchmarking
On the device:

    cd /storage/roms/ports/spectrax
    ./spectrax_bench --bench=all --device rg35xxsp --out=/storage/roms/ports/spectrax/runs

or over SSH from the host:

    scripts/deploy-spectrax.sh --device rg35xxsp --launch --out=runs/rg35xxsp

The report is a directory with `summary.json`, `timeseries.csv`,
`console.log` and `hostinfo.txt`. Read it with:

    scripts/show-runs.sh runs/rg35xxsp/<timestamp>

## Known device quirks
- The lid triggers ROCKNIX fake suspend. A bench run interrupted this way
  leaves a partial report (the bench flushes on SIGHUP/SIGTERM).
- The speaker and the headphone jack share an analog mux on the codec; the
  default ALSA route is the speaker.
- `/tmp` may be a tmpfs, so the fileio bench must be pointed at the real
  storage volume (`--workdir`; the profile's `paths.data_dir` is the default).
- The bench must not spawn an X server on the device: with `--device` set it
  never does.
