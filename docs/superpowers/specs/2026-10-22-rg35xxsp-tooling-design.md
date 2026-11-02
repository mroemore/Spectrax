# Design: Anbernic RG35XX SP Tooling — a device profile on the shared Spectrax bench/deploy rig

**Date:** 2026-10-22
**Branch:** `rg35xxsp-tooling` from `main` (local-only; not pushed — project rule #1078)
**Target:** Anbernic RG35XX SP (clamshell, Allwinner H700, 4× Cortex-A53 @ 1.5 GHz, Mali-G31 MP2, 1 GB RAM, 3.5" IPS 640×480)
**CFW in scope today:** ROCKNIX (`ROCKNIX-H700.aarch64`, mainline kernel, Panfrost, Sway + EmulationStation). GarlicOS / KNULLI / MinUI / stock Anbernic are *not* assumed — see [Open items](#9-risks--open-items-every-unknown-has-a-named-probe).
**Sibling document:** `docs/superpowers/specs/2026-09-20-gkd-pixel2-tooling-design.md` (GKD Pixel 2) and `docs/superpowers/plans/2026-09-20-gkd-pixel2-tooling.md` (GKD plan) on branch `gkd-pixel2-tooling`.

---

## 0. The one-paragraph version

The GKD Pixel 2 work already specifies a cross-compile rig, a `spectrax_bench` stress binary, a WiFi-SSH deploy pipeline and a four-tier pre-device testing ladder. **None of that code exists yet** — the GKD branch carries only two documents and the two meson cross files (`9648016`). So this is not "add a second device to a finished rig"; it is "define the rig once, with the device as data". This document specifies the **device-profile abstraction** that both the GKD Pixel 2 and the RG35XX SP plug into, and records the RG35XX SP's hardware facts with an explicit verified/assumed split. Every hardware fact this document could not verify is written as an open item with a named probe script (`probe-os.sh`, `probe-usb.sh`, `probe-paths.sh`, `probe-display.sh`, `probe-audio.sh`, `probe-cpu.sh`, `probe-lid.sh`, `gpu-check.sh`), exactly as the GKD spec does with `gpu-check.sh` / `inspect-sd.sh`. **No hardware fact in this document is asserted without a source or an explicit open item.**

The headline technical difference between the two devices is **transport**: the GKD Pixel 2 has no USB communications and needs a WiFi dongle; the RG35XX SP has built-in 2.4/5 GHz WiFi *and* a USB-C port wired to the SoC's OTG controller with `dr_mode = "peripheral"`, a Type-C role switch on the AXP717 PMIC, and a kernel built with USB gadget support (`CONFIG_USB_F_NCM`, `CONFIG_USB_F_RNDIS`, `CONFIG_USB_CONFIGFS_*`, `CONFIG_USB_MUSB_SUNXI`). ROCKNIX ships a `usbgadget` helper that exposes exactly this as an EmulationStation "Network Settings → gadget mode" toggle. That makes **USB-gadget networking a plausible primary transport** — but it is *plausible*, not *verified on this unit*, and it is the single most valuable thing to probe first.

The second headline is a **shared gap that the GKD plan has not noticed**: the repository ships prebuilt `lib/arm/` raylib + PortAudio + ALSA (ELF 32-bit ARM) and x86-64 `lib/linux/` raylib, but **no aarch64 libraries at all**, and `meson.build` hardcodes `libdir = lib/linux`. The GKD plan's *primary* target is aarch64. Both devices therefore need the same missing artefact (an aarch64 raylib/PortAudio bundle) — so it is a shared task, not a device task, and this document says so.

---

## 1. Goal

Provide a cross-compile + deploy + benchmark pipeline for the Anbernic RG35XX SP **by reusing the GKD Pixel 2 tooling**, such that:

1. There is **one** `deploy-spectrax.sh`, **one** `spectrax_bench`, **one** cross-file generator and **one** probe suite, parameterised by a checked-in **device profile**.
2. The GKD Pixel 2 keeps working unchanged, by retrofitting its profile onto the shared scripts (an explicit, separately-tracked task).
3. The RG35XX SP's hardware facts are recorded with a verified/assumed split, and every unknown has a named probe.
4. The pre-device testing ladder is reused where it applies and explicitly *not* reused where it does not (Tier C system emulation is RK3326-specific).

## 2. Non-goals

- Optimising the app itself. The bench *measures* candidates; it does not implement them.
- Writing any of the code in this document. This is a design + plan pair; implementation is a separate, later exercise.
- Supporting a third device. The profile schema is written so a third device is a data change, but only two profiles ship.
- Choosing the user's CFW for them. This document assumes ROCKNIX as the most likely target and marks the choice as an open item with a probe.
- File-watcher / run-on-save automation, CI integration, bench history dashboard — deferred, exactly as in the GKD spec.

## 3. Scope (delivered by this spec + its plan)

1. A **device-profile schema** (`devices/<id>.toml`) with a loader, a linter and unit tests.
2. A **cross-file generator** that turns a profile into a meson cross file (CPU flags, lib dir, platform properties).
3. A **parameterised script set** replacing the GKD plan's device-specific scripts: `discover-device.sh`, `setup-device.sh`, `deploy-spectrax.sh`, `deploy-sd.sh`, `inspect-sd.sh`, `gpu-check.sh`, `probe-*.sh`.
4. A **shared `spectrax_bench`** with a device-profile-aware display/IO layer.
5. An **RG35XX SP profile** with verified hardware facts and an explicit list of open items, each with a probe.
6. **Retrofitting the GKD Pixel 2 profile** onto the shared scripts, as its own tracked task.

## 4. Branch and document placement

- Branch `rg35xxsp-tooling`, cut from `main` (matching how `gkd-pixel2-tooling` was cut from `origin/main`).
- This spec: `docs/superpowers/specs/2026-10-22-rg35xxsp-tooling-design.md`.
- The plan: `docs/superpowers/plans/2026-10-22-rg35xxsp-tooling.md`.
- The project's `.gitignore` excludes `docs/*` (`docs/*` appears twice in `.gitignore`). Both documents are therefore force-added: `git add -f docs/superpowers/specs/… docs/superpowers/plans/…`.
- Nothing is pushed. Project rule #1078.

---

## 5. Device facts — the RG35XX SP specifically

**Do not conflate with the RG35XX (2024), RG35XX H, RG35XX Plus, RG35XX Pro, RG34XX SP or the 2026 RG SP.** The RG35XX SP is the 2024 clamshell that inherits its device tree from the RG35XX Plus (`sun50i-h700-anbernic-rg35xx-sp.dts` `#include`s `sun50i-h700-anbernic-rg35xx-plus.dts`), and it is *not* the same machine as the RG35XX H (also H700, but a different panel/DTB and a different PCB revision).

### 5.1 Verified facts

Each row states the source that was actually read. Sources are URLs that a later implementer can re-fetch.

| # | Fact | Value | Source |
|---|------|-------|--------|
| V1 | Model / form factor | Anbernic RG35XX SP, clamshell GBA-SP-style, released June 2024 | <https://en.wikipedia.org/wiki/Anbernic_RG35XXSP> |
| V2 | SoC | Allwinner H700 (`allwinner,sun50i-h700`), sunxi family | Anbernic product page; ROCKNIX wiki device page; mainline DTS `compatible = "anbernic,rg35xx-sp", "allwinner,sun50i-h700"` |
| V3 | CPU | 4× ARM Cortex-A53. Device-tree OPP table runs 480 MHz → **1512 MHz** | `arch/arm64/boot/dts/allwinner/sun50i-h616.dtsi` (`compatible = "arm,cortex-a53"` ×4), `sun50i-h616-cpu-opp.dtsi` (`opp-hz` 480000000 … 1512000000) |
| V4 | CPU clock (vendor) | quad-core Cortex-A53 @ 1.5 GHz | Anbernic product page |
| V5 | CPU clock (ROCKNIX) | quad-core A53 @ 1.4 GHz; an EmulationStation "overclock" option enables 1.5 GHz | <https://rocknix.org/devices/anbernic/rg35xx-sp/> |
| V6 | GPU | Mali-G31 (Anbernic markets it as dual-core Mali-G31 MP2). Device tree node `compatible = "allwinner,sun50i-h616-mali", "arm,mali-bifrost"` | Anbernic product page; ROCKNIX wiki; `sun50i-h616.dtsi` |
| V7 | GPU driver (ROCKNIX) | **Panfrost**, advertised as GL 3.1 / GLES 3.1 | ROCKNIX wiki device page |
| V8 | Kernel | Mainline Linux. ROCKNIX's H700 kernel is built **aarch64** (`Linux/arm64 7.2.0`, `CONFIG_CC_VERSION_TEXT="aarch64-rocknix-linux-gnu-gcc-16.2.0"`) | ROCKNIX `projects/ROCKNIX/devices/H700/linux/linux.aarch64.conf` |
| V9 | Panel | 3.5-inch IPS, **640×480**, 4:3 | Anbernic product page; ROCKNIX wiki; Wikipedia |
| V10 | RAM | 1 GB. **Vendor says DDR3; the ROCKNIX wiki says LPDDR4 — these conflict.** | Anbernic product page vs ROCKNIX wiki device page |
| V11 | Storage | No eMMC; microSD only | ROCKNIX wiki device page |
| V12 | WiFi + Bluetooth | **Built in.** SDIO WiFi `realtek,rtl8821cs` + Bluetooth `realtek,rtl8821cs` | `sun50i-h700-anbernic-rg35xx-plus.dts` (which the SP's DTS includes) |
| V13 | PMIC | X-Powers **AXP717**, with a USB Type-C role switch (`x-powers,axp717-usb-power-supply`, 1.5 A input limit) | `sun50i-h700-anbernic-rg35xx-2024.dts` |
| V14 | USB | One USB-C receptacle, wired to the SoC OTG controller as `&usbotg { dr_mode = "peripheral"; }`, commented "USB type-C receptacle"; `reg_vcc5v` is the "USB-C power input" | `sun50i-h700-anbernic-rg35xx-2024.dts` |
| V15 | Kernel USB-gadget support | `CONFIG_USB_GADGET=y`, `CONFIG_USB_F_NCM=y`, `CONFIG_USB_F_ECM=y`, `CONFIG_USB_F_RNDIS=y`, `CONFIG_USB_CONFIGFS_ECM=y`, `CONFIG_USB_CONFIGFS_RNDIS=y`, `CONFIG_USB_OTG=y`, `CONFIG_USB_MUSB_SUNXI=y`, `CONFIG_USB_MUSB_DUAL_ROLE=y` | ROCKNIX `linux.aarch64.conf` |
| V16 | USB gadget userspace | ROCKNIX ships `/usr/bin/usbgadget` with modes `disabled` / `network` (USB **NCM**) / `file_transfer` (MTP), started by autostart script `081-usbgadget`; the H700 platform has a `sleep.d/post/005-usbgadget` quirk because "musb drops the USB session on suspend and does not restart it on resume" | ROCKNIX `packages/rocknix/sources/scripts/usbgadget`, `packages/rocknix/autostart/081-usbgadget`, `packages/hardware/quirks/platforms/H700/sleep.d/post/005-usbgadget` |
| V17 | USB gadget addressing | Device side `169.254.170.2`, host side `169.254.170.1` by default; `usbgadget address` prints the current address | ROCKNIX `usbgadget` source (`configure_iface`, `network.conf`) |
| V18 | User interface | Sway (Wayland compositor) + EmulationStation | ROCKNIX wiki device page |
| V19 | Storage layout | Writable root is `/storage`; ROMS are exposed at `/storage/roms`; the ports game path is `roms/ports` | ROCKNIX wiki (Add Games, Ports) |
| V20 | PortMaster path | PortMaster's own install instructions list `/roms/ports/` for ROCKNIX | <https://portmaster.games/installation.html> |
| V21 | SSH / Samba | Disabled by default in release builds; enabled in EmulationStation → Network Settings. Username is `root`. **The root password is regenerated on every reboot** and displayed in System Settings; the user can set their own | ROCKNIX wiki FAQ; ROCKNIX source tree contains `scripts/setrootpass` |
| V22 | Suspend | No hardware suspend. ROCKNIX ships an `h700-suspend-stub` package and a "fake suspend" feature: power button **or lid close** triggers it; configurable in ES System Settings → Suspend (delay, offline-CPU count, charger/HDMI exemptions) | ROCKNIX wiki <https://rocknix.org/configure/fake-suspend/>; ROCKNIX `packages/h700-suspend-stub`; `packages/hardware/quirks/platforms/H700/sleep.d/post/005-usbgadget` |
| V23 | Lid switch | The mainline device tree defines a `gpio-keys` `lid-switch` (`linux,code = <SW_LID>`, GPIO PE7, `linux,can-disable`, `wakeup-source`, `wakeup-event-action = <EV_ACT_DEASSERTED>`) | `arch/arm64/boot/dts/allwinner/sun50i-h700-anbernic-rg35xx-sp.dts` |
| V24 | Audio | Allwinner codec; `allwinner,audio-routing = "Speaker", "LINOUT", "Headphone", "LINOUT"` (speakers and headphone jack share a 74HC4052D analog mux), PA enable GPIO PI5, headphone-detect GPIO PI3. `CONFIG_SND_SOC=y` in the H700 kernel | `sun50i-h700-anbernic-rg35xx-2024.dts`; `linux.aarch64.conf` |
| V25 | Battery | Design voltage 4.2 V, charge current limit 1.024 A (consistent with the advertised 3300 mAh cell) | `sun50i-h700-anbernic-rg35xx-2024.dts` |
| V26 | Device tree / DTB | `sun50i-h700-anbernic-rg35xx-sp.dtb`; a v2 panel variant `sun50i-h700-anbernic-rg35xx-sp-v2-panel.dtb` also exists (the two are not interchangeable — wrong DTB gives a garbled screen) | ROCKNIX H700 installation guide <https://rocknix.org/configure/h700-installation/> |
| V27 | ROCKNIX image | `ROCKNIX-H700.aarch64` builds exist for the RG35XX SP | ROCKNIX release page / distribution repo |
| V28 | Buttons | Two extra action buttons (X, Y), two extra shoulder buttons, a MENU button, a power button and a reset key, a volume rocker. **No analog sticks.** | Wikipedia; Anbernic product page |
| V29 | PortMaster support | ROCKNIX documents PortMaster for its ports; PortMaster states it is ARM-only | ROCKNIX wiki Ports page; portmaster.games |

### 5.2 Repository facts (verified in this repo, not from the web)

These matter as much as the device facts, because they decide how much of the rig is actually reusable.

| # | Fact | Source |
|---|------|--------|
| R1 | `lib/arm/libraylib.a`, `lib/arm/libportaudio.a`, `lib/arm/libasound.a` are **ELF 32-bit ARM (armhf)** — `file` on the extracted `rcore.o` reports "ELF 32-bit LSB relocatable, ARM, EABI5" | `file lib/arm/libraylib.a`, `ar x` + `file rcore.o` |
| R2 | The armhf raylib's GLFW object references **`libEGL.so`, `libGLESv2.so`, `libGL.so` and `libX11.so`** — i.e. EGL/GLES2 and X11 are both present in the build. It is *not* proven that GLFW selects EGL at runtime, and there is no Wayland/GLFW-native reference | `strings lib/arm/libraylib.a` over all members |
| R3 | `lib/linux/libraylib.a` is the x86-64 desktop-GL build (references `libGL.so`, `libGL.so.1`) | `strings lib/linux/libraylib.a` |
| R4 | **There are no aarch64 libraries anywhere in the repository.** The complete set of `.a` files is `include/libraylib.a`, `lib/linux/libraylib{,dll}.a`, `lib/linux/libkissfft-float.a`, `lib/win/libraylib{,dll}.a`, `lib/win/raylib.dll`, `lib/arm/libraylib.a`, `lib/arm/libportaudio.a`, `lib/arm/libasound.a` | `find . -name '*.a'` |
| R5 | `meson.build` hardcodes `libdir = meson.current_source_dir() / 'lib' / 'linux'` and looks up `raylib`, `kissfft-float` and `portaudio` from it. A cross build therefore links **x86-64** libraries unless this changes | `meson.build` lines 30–33 |
| R6 | `include/arm/` holds the armhf headers (`raylib.h`, `raymath.h`, `portaudio.h`); the top-level `include/` holds the x86-64 headers plus a stray `include/libraylib.a` | `ls include/ include/arm/` |
| R7 | `scripts/` does not exist on `main`; the GKD branch's scripts are planned, not written | `ls scripts/` → "No such file or directory" |
| R8 | `src/tools/` contains `nav_harness`, `instrument_harness`, `sample_analyser`, `vizulobe` — there is no `bench/` yet | `ls src/tools/` |
| R9 | The app renders a 640×480 surface (`src/main.c:881` initialises the time graph with `640, 1024, 128`; the window size is set at runtime) | `grep 640 src/main.c` |

### 5.3 Assumed (not verified — carried as open items)

| # | Assumption | Why it is only an assumption | Open item |
|---|-----------|-------------------------------|-----------|
| A1 | The user's RG35XX SP will run **ROCKNIX** | It is the best-documented H700 image, but the user may prefer GarlicOS (Anbernic's own), KNULLI, MinUI or the stock Anbernic OS. All of these differ in userspace arch, path layout, SSH policy and GPU stack | §9.1 |
| A2 | The userspace is **aarch64** | Verified for ROCKNIX H700 (V8). Not verified for GarlicOS/KNULLI/MinUI/stock. GarlicOS in particular ships a mix | §9.1 |
| A3 | The USB-C port will actually **enumerate as a USB gadget** on this unit | The DTS says `dr_mode = "peripheral"` and the kernel has gadget support, but the AXP717 Type-C role switch has to be flipped to "device" mode by `usbgadget`'s `set_role device`, and the physical cable must carry data (SDP/CDP, not a charge-only port) | §9.2 |
| A4 | ROCKNIX's EmulationStation "Network Settings" exposes the gadget-mode toggle **on this device** | `usbgadget` is a generic script, but the ES menu wiring is per-platform | §9.2 |
| A5 | The ports directory is **`/storage/roms/ports`** | ROCKNIX says `/storage/roms`, PortMaster says `/roms/ports`. Both may be true (`/roms` may be a symlink); the tree was not inspected | §9.3 |
| A6 | The panel is driven at **640×480 with no compositor scaling** | The panel is 640×480 (V9) but Sway's scaling/`viewporter` behaviour was not observed | §9.4 |
| A7 | The clock can be held at **1.5 GHz** | V5 says an OC option enables 1.5 GHz; whether that is a cpufreq OPP, a firmware switch or a one-shot governor change is unknown | §9.6 |
| A8 | **PortAudio's ALSA backend** works against the H700 codec with the device's native sample rate | `lib/arm` ships a PortAudio+ALSA pair for armhf (R1) but the H700 codec's ALSA card/rate behaviour is unobserved | §9.5 |
| A9 | A raylib built as **`PLATFORM_DRM` + `GRAPHICS_API_OPENGL_ES2`** will get a working EGL/GLES2 context on this device under Sway | The vendored armhf raylib shows EGL/GLES symbols (R2) but no aarch64 build exists (R4) and the DRM/GBM/EGL stack under Sway is unobserved | §9.4, §9.7 |
| A10 | RAM is **DDR3** (Anbernic) rather than LPDDR4 (ROCKNIX wiki) | Direct contradiction between two vendor-side sources; the U-Boot DDR init package in ROCKNIX is named `u-boot-DDR3` for this device family, which leans DDR3, but the SP's own DRAM init was not read | §9.8 |
| A11 | The device boots in **under ~60 s** | Unknown; ROCKNIX boots an EmulationStation frontend plus a compositor | §9.9 |
| A12 | The stock Anbernic OS (and GarlicOS) expose **ADB** | Widely reported for the Android-derived Anbernic OSes; the RG35XX SP's Linux-based stock OS is a different thing. Not verified, and not needed if ROCKNIX is the target | §9.10 |

### 5.4 What this means for the app (not just the tooling)

- **Display:** 640×480 native, exactly matching the Spectrax surface. *No scaling work is needed* — unlike a Miyoo Mini (320×240) or a Miyoo Mini Plus (640×360). This is a rare win and is the reason the RG35XX SP is a good second target.
- **CPU:** Cortex-A53 is out-of-order with 128-bit NEON, versus the GKD's in-order Cortex-A35 with narrower NEON. Bench numbers on the SP will be *better* than on the Pixel 2 for the same workload; the two must not be compared without normalising for the microarchitecture, not just the clock.
- **GPU:** Panfrost is a proper open-source GLES3 driver; the GKD's ROGUE OS libmali story is murkier. GLES3 context acquisition is likely *easier* here, but a DRM/KMS/GBM/EGL raylib build is required for **both** devices.
- **Compositor:** Sway (Wayland). A GLFW build without Wayland support will not open a window without XWayland. This applies to the GKD too (ROGUE OS is ROCKNIX-based).
- **Audio:** speaker and headphone jack share an analog mux on the codec, so the default ALSA route is likely `Speaker`; the headphone route must be selected explicitly.
- **Clamshell:** closing the lid triggers ROCKNIX "fake suspend" — the app can be killed or frozen mid-session. The app (not the tooling) must decide what to do; the tooling needs a "relaunch after resume" affordance.

---

## 6. The device-profile abstraction

### 6.1 Principles

1. **The device is data.** A device is a TOML file. No script forks.
2. **Facts live in the repo; secrets and IP live on the host.** `devices/<id>.toml` is committed. `~/.config/spectrax/devices/<id>.toml` overrides it for credentials, last-known IP and build-dir overrides.
3. **Profiles are validated.** A linter refuses unknown keys, missing required keys and contradictory values. A profile that fails to lint never reaches a deploy.
4. **One loader, one set of scripts.** Every script reads the profile through the loader. There is no `deploy-spectrax-rg35xxsp.sh`.
5. **Probes are the source of truth for unknowns.** Where a profile field is marked `probed = false`, the plan ships a probe script that fills it in, and the profile records the probe's output.

### 6.2 Where profiles live

```
devices/
  gkd-pixel2.toml          # committed device profile
  rg35xxsp.toml            # committed device profile
devices/schema/
  device-profile.schema.md  # the normative key list (human-readable; the linter enforces it)
```

Host-local overrides (never committed):

```
~/.config/spectrax/devices/<id>.toml
```

Resolution order: host override file > repo profile file, key by key.

### 6.3 Schema

```toml
# devices/rg35xxsp.toml — every key below is explained in §6.4.
[device]
id        = "rg35xxsp"          # slug; matches the file name
vendor    = "Anbernic"
model     = "RG35XX SP"
soc       = "Allwinner H700"
arch      = "aarch64"           # userspace ABI of the CFW actually installed
cpu_core  = "cortex-a53"
cpu_mhz   = 1500
cores     = 4
gpu       = "Mali-G31 MP2"
ram_mb    = 1024
ram_type  = "DDR3"              # disputed with the ROCKNIX wiki — see A10
storage   = "microSD"           # no eMMC

[cfw]
family         = "rocknix"      # rocknix | garlic | knulli | minui | anbernic-stock
hostname       = "RG35XX-SP"    # as reported by `uname -n` / ES
user           = "root"
password       = ""             # empty => password_source decides
password_source = "user-set"    # fixed | user-set | device-display
ssh_enabled_by_default = false   # ROCKNIX ships with SSH off

[display]
width   = 640
height  = 480
scale   = 1.0                   # compositor scale factor
native  = true                  # true => app surface == panel resolution

[gpu]
driver      = "panfrost"        # panfrost | libmali | lima
api         = "gles3"           # gles2 | gles3
helper      = ""                # optional GPU-switch helper, e.g. /usr/bin/setgpu.sh
compositor  = "sway"            # sway | x11
platform    = "drm"             # raylib PLATFORM_ for the cross build

[paths]
storage_root = "/storage"
ports_dir    = "/storage/roms/ports"
data_dir     = "/storage/roms/ports/spectrax"
runs_dir     = "/storage/roms/ports/spectrax/runs"

[transport]
order         = ["usb-gadget", "wifi-ssh", "sd"]
mdns_names    = ["rocknix.local", "rg35xxsp.local"]
usb_gadget_host_ip   = "169.254.170.1"
usb_gadget_device_ip = "169.254.170.2"

[build]
cross_file = "meson/cross/aarch64-linux-gnu.ini"
libdir     = "lib/aarch64"
c_args     = ["-mcpu=cortex-a53", "-mtune=cortex-a53", "-O3",
              "-ftree-vectorize", "-ffast-math", "-flto"]

[probes]
# Keys whose value is not yet established by a probe. The linter fails the
# profile if any of these is still true when a deploy is attempted.
needed = ["os_release", "userspace_arch", "ports_dir", "usb_gadget",
          "display_mode", "audio_device", "cpu_governor", "lid_switch"]
```

### 6.4 Filled-in profiles side by side

The point of this table is to show that the *only* differences between the two devices are data.

| Key | GKD Pixel 2 | RG35XX SP | Comment |
|-----|-------------|-----------|---------|
| `device.id` | `gkd-pixel2` | `rg35xxsp` | slug |
| `device.vendor` / `model` | GKD / Pixel 2 | Anbernic / RG35XX SP | |
| `device.soc` | RK3326S | Allwinner H700 | |
| `device.arch` | `aarch64` (probed; the GKD CFW may be armhf) | `aarch64` (ROCKNIX H700, verified) | |
| `device.cpu_core` | `cortex-a35` | `cortex-a53` | drives `-mcpu`/`-mtune` |
| `device.cpu_mhz` | 1500 | 1500 (or 1400 stock / 1500 OC) | coincidence; do **not** read this as "same perf" |
| `device.gpu` | Mali-G31 MP2 | Mali-G31 MP2 | coincidence — same GPU family, different SoC integration |
| `device.ram_mb` | 1024 | 1024 | |
| `device.ram_type` | DDR3 | DDR3 (disputed, A10) | |
| `device.storage` | microSD | microSD | |
| `cfw.family` | `rogue` (ROGUE OS / ROCKNIX-based) | `rocknix` | |
| `cfw.hostname` | `rogue_pixel2` | `RG35XX-SP` | used for mDNS discovery |
| `cfw.user` | `root` | `root` | |
| `cfw.password` | `rogue` | *(empty)* | |
| `cfw.password_source` | `fixed` | `user-set` | **the one behavioural difference in setup** |
| `cfw.ssh_enabled_by_default` | `true` (ROGUE OS ships sshd on) | `false` (SSH off until enabled in ES) | |
| `display.width`/`height` | 640 / 480 | 640 / 480 | **identical** |
| `display.scale` | 1.0 | 1.0 | |
| `display.native` | true | true | |
| `gpu.driver` | `libmali` (unverified — GKD open item) | `panfrost` (verified, V7) | |
| `gpu.api` | `gles2` | `gles3` | |
| `gpu.helper` | `/usr/bin/setgpu.sh` (a guess in the GKD spec) | *(none known)* | |
| `gpu.compositor` | `sway` | `sway` | |
| `gpu.platform` | `drm` | `drm` | |
| `paths.storage_root` | `/roms` | `/storage` | |
| `paths.ports_dir` | `/roms/ports` | `/storage/roms/ports` | |
| `transport.order` | `["wifi-ssh", "sd"]` | `["usb-gadget", "wifi-ssh", "sd"]` | **the headline difference** |
| `transport.mdns_names` | `["rogue_pixel2.local"]` | `["rocknix.local", "rg35xxsp.local"]` | |
| `transport.usb_gadget_host_ip` | n/a | `169.254.170.1` | |
| `build.cross_file` | `meson/cross/aarch64-linux-gnu.ini` | same | **same file** |
| `build.libdir` | `lib/aarch64` | `lib/aarch64` | **same** |
| `build.c_args` | `-mcpu=cortex-a35 -mtune=cortex-a35 …` | `-mcpu=cortex-a53 -mtune=cortex-a53 …` | only the `-mcpu` pair differs |

The two profiles differ in **17 leaf values**. Everything else — every script, every binary, every cross-file template — is identical.

### 6.5 How scripts consume a profile

The loader is `scripts/lib/profile.py` (Python 3, host-side only; the repo already assumes `python3` in its test commands). It exposes:

```
scripts/lib/profile.py --list                      # every known profile id
scripts/lib/profile.py --shell <id>                # emit `KEY=value` shell assignments
scripts/lib/profile.py --get <id> <section.key>    # single value
scripts/lib/profile.py --lint <id>                # validate; non-zero exit on error
scripts/lib/profile.py --resolve <id> <key>        # repo profile merged with host override
```

Scripts consume it as:

```sh
device=${SPECTRAX_DEVICE:-rg35xxsp}
eval "$(scripts/lib/profile.py --shell "$device")"
# now $DEVICE_ARCH, $PATHS_PORTS_DIR, $TRANSPORT_ORDER, $BUILD_C_ARGS … are set
```

`--shell` emits only a fixed allow-list of key names that the linter has already validated, so `eval` cannot be fed arbitrary text.

For shell scripts that also want the profile's values *and* the device-specific
script directory on `PATH`, the loader is sourced rather than called:

```sh
SPECTRAX_DEVICE="$device"
. scripts/lib/profile.sh
```

**The device id travels through the environment, not through an argument.**
`dash` — `/bin/sh` on Void Linux — does not pass arguments to a sourced file,
so `. profile.sh <id>` would silently read the *caller's* first positional
parameter. `$1` is honoured as a fallback for bash users, but every in-repo
caller sets `SPECTRAX_DEVICE` first.

---

## 7. Reuse matrix

**Legend.** **SHARED** = used byte-for-byte by both devices. **PARAM** = shared code, device-specific data (a profile field). **EXTEND** = shared code plus an additive, device-conditional branch that lives in the shared file (no fork). **DEVICE** = genuinely separate code, with the reason stated.

### 7.1 Build system

| GKD component | Class | RG35XXSP delta | Where the difference lives |
|---|---|---|---|
| `meson/cross/aarch64-linux-gnu.ini` | **PARAM** | `-mcpu/-mtune` become `cortex-a53`; a `[properties]` block carries `spectrax_libdir` and `spectrax_platform` | generated by `scripts/gen-cross-file.sh` from the profile; the committed file becomes an arch-generic template |
| `meson/cross/arm-linux-gnueabihf.ini` | **SHARED** | not selected — ROCKNIX H700 is aarch64-only (V8) | kept for the GKD's armhf fallback and for any future armhf CFW |
| `meson.build` libdir lookup | **EXTEND** | `lib/linux` → profile-selected dir (`lib/aarch64`) | one line: `meson.get_external_property('spectrax_libdir', 'lib/linux')` |
| `lib/aarch64/` raylib + PortAudio + ALSA bundle | **SHARED (missing today)** | identical artefact | **R4: this does not exist yet.** Shared task. Both devices need it |
| `src/tools/bench/*` (all modules) | **SHARED / PARAM** | see §7.4 | profile-driven CLI flags |
| `tests/dsp/test_bench_report.c` | **SHARED** | none | |
| `tests/dsp/test_qemu_smoke.sh` | **PARAM** | arch comes from the profile | one `--arch` argument |

### 7.2 Scripts — host side

| GKD component | Class | RG35XXSP delta | Notes |
|---|---|---|---|
| `scripts/setup-qemu-binfmt.sh` | **SHARED** | none — aarch64 is already registered | |
| `scripts/setup-audio-dummy.sh` | **SHARED** | none — host-only | |
| `scripts/cpu-clamp.sh` | **SHARED** | none — host-only | The *device* clock is 1.5 GHz on both, so Tier B's clamp target is coincidentally the same number; the microarchitecture is not |
| `scripts/run-bench-qemu.sh` | **SHARED** | none | |
| `scripts/show-runs.sh` | **SHARED** | none | |
| `scripts/extract-armbian-rk3326.sh` | **DEVICE** | not applicable | No Armbian H700 image exists. H700 also has no qemu-system model — see §8.4 |
| `scripts/run-qemu-rk3326.sh` | **DEVICE** | replaced by a generic `qemu-system-aarch64 -M virt` approximation, if attempted at all | §8.4 |
| `scripts/gen-cross-file.sh` (new) | **SHARED** | profile-driven | replaces per-device cross files |
| `scripts/lib/profile.py` (new) | **SHARED** | — | profile loader + linter |

### 7.3 Scripts — device side

| GKD component | Class | RG35XXSP delta | Notes |
|---|---|---|---|
| `scripts/discover-pixel.sh` | **PARAM** → `discover-device.sh` | hostname/mDNS list from the profile; **plus a USB-gadget probe** that looks for `169.254.170.1` on the host's interfaces and for a `usb0`/`gadget` NIC | The RG35XXSP can be found over USB *before* WiFi is even configured |
| `scripts/setup-pixel.sh` | **PARAM** → `setup-device.sh` | credential source differs: GKD has a fixed default password (`rogue`); ROCKNIX **regenerates the root password on every reboot** and shows it in ES System Settings, and also ships `setrootpass` | The *mechanism* is parameterised (`password_source`), the *instructions* differ. The RG35XXSP flow is: user sets a root password once in System Settings, then key-install proceeds unattended |
| `scripts/deploy-spectrax.sh` | **PARAM** | transport list from the profile: `usb-gadget` → `wifi-ssh` → `sd` | Same script, three transports, ordered. The GKD simply has no `usb-gadget` entry |
| `scripts/deploy-sd.sh` | **PARAM** | destination from `paths.ports_dir` | |
| `scripts/inspect-sd.sh` | **PARAM** | searches both `/roms/ports` and `/storage/roms/ports` | the two documents disagree (V19/V20) — the inspector must tolerate either |
| `scripts/gpu-check.sh` | **EXTEND** → `probe-gpu.sh` | adds Panfrost-specific probes (`/sys/class/drm/card*/device/uevent`, `dmesg \| grep -i panfrost`, EGL vendor string) | The GKD branch probes libmali; the SP branch probes Panfrost. Both live in the same script, selected by `gpu.driver` |
| `scripts/portmaster/spectrax.sh` | **PARAM** | `cd` into the profile's `paths.data_dir`; the GPU-helper preamble is a no-op on the SP (no helper known) | |
| `scripts/portmaster/spectrax_bench.sh` | **PARAM** | as above; output dir from `paths.runs_dir` | |
| `scripts/probe-os.sh` (new) | **SHARED** | — | fills `cfw.family`, `device.arch`, `cfw.hostname`, `password_source` |
| `scripts/probe-usb.sh` (new) | **SHARED** | — | fills `transport.usb_gadget.*`, decides whether `usb-gadget` stays in `transport.order` |
| `scripts/probe-paths.sh` (new) | **SHARED** | — | resolves `paths.ports_dir` |
| `scripts/probe-display.sh` (new) | **SHARED** | — | resolves `display.scale`, `display.native`, `gpu.api` |
| `scripts/probe-audio.sh` (new) | **SHARED** | — | resolves the ALSA card/device and native sample rate |
| `scripts/probe-cpu.sh` (new) | **SHARED** | — | resolves the cpufreq governors, the max OPP and whether 1.5 GHz is reachable |
| `scripts/probe-mem.sh` (new) | **SHARED** | — | resolves `ram_mb`, `ram_type` (the DDR3/LPDDR4 dispute) and the free-memory headroom |
| `scripts/probe-lid.sh` (new) | **SHARED** | — | resolves whether `SW_LID` events are delivered and what fake suspend does to a running process |
| `scripts/probe-boot.sh` (new) | **SHARED** | — | measures the power-on → SSH-reachable budget |
| `scripts/probe-storage.sh` (new) | **SHARED** | — | characterises the SD card's write throughput and identity |
| `scripts/lib/profile.sh` (new) | **SHARED** | — | the sourced shell view of a profile; also prepends `scripts/devices/<id>/` to `PATH` |
| `scripts/lib/probe.sh` (new) | **SHARED** | — | the shared probe scaffolding (argument parsing, SSH wrapper, `--apply` writer) |

### 7.4 `spectrax_bench` — shared vs device-specific

| Concern | Class | Note |
|---|---|---|
| `bench_report` (JSON + CSV + console + hostinfo) | **SHARED** | nothing device-specific in it |
| CLI dispatch (`main.c`) | **PARAM** | gains `--device <id>`, `--display WxH`, `--workdir DIR`, `--gpu <api>`; stamps the resolved profile into `summary.json` under `meta.device` |
| `bench_xvfb` | **PARAM** | screen size from the profile; gains a "no display needed" path for on-device runs (the bench must **not** try to spawn Xvfb on the device) |
| `bench_audio` | **SHARED / PARAM** | The DSP loop is device-agnostic and deliberately avoids PortAudio. The buffer size and sample rate become profile fields so the numbers mean something next to the device's real ALSA rate |
| `bench_fileio` | **PARAM** | the working directory must be the device's real storage (`paths.data_dir` or `/storage`), **not** `/tmp` — on ROCKNIX `/tmp` may be a tmpfs and would measure RAM, not the SD card |
| `bench_gfx` | **PARAM** | window size from the profile (640×480 on both); the device build must use the DRM/EGL path rather than X11 |
| Scalar-vs-NEON toggle (`--no-neon`) | **SHARED** | on an A53 the NEON width is 128-bit, so the gain will differ from the A35 — that is the point of measuring it |

### 7.5 Summary counts

Counting one row once, under its dominant class (a row marked *shared/param* counts as shared when the shared part dominates):

| Class | Rows | Items |
|---|---|---|
| SHARED | 24 | armhf cross template, the aarch64 lib bundle, `test_bench_report.c`, `setup-qemu-binfmt.sh`, `setup-audio-dummy.sh`, `cpu-clamp.sh`, `run-bench-qemu.sh`, `show-runs.sh`, `gen-cross-file.sh`, `lib/profile.py`, `lib/profile.sh`, `lib/probe.sh`, the 10 `probe-*.sh` scripts, `bench_report`, the `--no-neon` toggle |
| PARAM | 15 | aarch64 cross generation, `src/tools/bench/*`, `test_qemu_smoke.sh`, `discover-device.sh`, `setup-device.sh`, `deploy-spectrax.sh`, `deploy-sd.sh`, `inspect-sd.sh`, both PortMaster wrappers, `bench` CLI dispatch, `bench_xvfb`, `bench_audio`, `bench_fileio`, `bench_gfx` |
| EXTEND | 2 | `meson.build` (one property lookup), `gpu-check.sh` → `probe-gpu.sh` (adds the Panfrost branch) |
| DEVICE | 2 | `extract-armbian-rk3326.sh`, `run-qemu-rk3326.sh` — both GKD-only, both moved under `scripts/devices/gkd-pixel2/` |
| **Total rows** | **43** | across §7.1–§7.4 |

The ratio is the point: **39 of 43 rows are shared or parameterised.** The two DEVICE rows are the RK3326 system-emulation helpers, which cannot be reused because there is no Armbian H700 image and QEMU has no H700 machine model.

---

## 8. Cross-compile story, deploy story, bench story, testing ladder

### 8.1 Cross-compile

The GKD plan's Task 1 writes two meson cross files with hardcoded `-mcpu=cortex-a35`. That cannot be right for two devices. The revised story:

1. The committed cross files become **arch-generic templates** with no `-mcpu`, plus a `[properties]` section:
   ```ini
   [properties]
   spectrax_libdir   = 'lib/aarch64'
   spectrax_platform = 'drm'
   ```
2. `scripts/gen-cross-file.sh <device>` reads the profile, copies the template, and injects the profile's `build.c_args` and `build.libdir` into the generated file at `build-<device>/cross.ini`.
3. `meson.build` reads the lib dir from the cross file's property, falling back to `lib/linux` for native builds:
   ```meson
   libdir = meson.current_source_dir() / meson.get_external_property('spectrax_libdir', 'lib/linux')
   ```
4. **The missing aarch64 libraries (R4) become a first-class task.** The repository already carries a *pattern* for this: `lib/arm/` + `include/arm/` hold armhf raylib/PortAudio/ALSA. The plan adds `lib/aarch64/` + `include/aarch64/` with:
   - **raylib** built `PLATFORM=PLATFORM_DRM`, `GRAPHICS_API_OPENGL_ES2` (matching the GKD plan's `GRAPHICS=OPENGL_ES2`), linking `libdrm`, `libgbm`, `libEGL`, `libGLESv2`.
   - **PortAudio** built with the ALSA backend, linking `libasound`.
   - **kissfft** is arch-independent C; either cross-compile it (trivial) or reuse the existing one.
   - Build recipe: a `scripts/build-target-libs.sh <arch>` that uses a meson "native" cross toolchain and lands the artefacts in `lib/<arch>/`.

   This is **shared** work, and it is the largest single blocker in the whole effort. The GKD plan treats cross-compiling as "write two ini files"; it is actually "produce a target lib bundle".

### 8.2 Deploy — transport reality

| | GKD Pixel 2 | RG35XX SP |
|---|---|---|
| WiFi | **None built in.** Needs a USB WiFi dongle (the GKD spec recommends a Plugable Nano, or USB-tether to a phone) | **Built in**, 2.4/5 GHz, RTL8821CS SDIO (V12) |
| USB data | None (per the GKD spec) | USB-C wired to `usbotg` as `peripheral` (V14); kernel has NCM/ECM/RNDIS gadget support (V15); ROCKNIX ships `usbgadget` (V16) |
| ADB | n/a | Unverified for the Linux-based OSes (A12) |
| SD card | Yes | Yes (no eMMC, V11) |
| Practical primary | WiFi-SSH over dongle | **USB-gadget SSH (`169.254.170.1` → device `169.254.170.2`), if the probe confirms it**; otherwise WiFi-SSH |
| Practical fallback | SD card swap | SD card swap (always available, and the only transport that needs nothing from the OS) |

The shared `deploy-spectrax.sh` serves **both devices unchanged** provided the transport list is data:

```
transport.order = ["usb-gadget", "wifi-ssh", "sd"]
```

`deploy-spectrax.sh` walks the list in order, using the first transport that is available, and reports which one it used. For the GKD the list simply has no `usb-gadget` entry.

**One device-specific behaviour, not a code fork:** the SD fallback differs in *where* it writes (the profile's `paths.ports_dir`) and in *how the SD is mounted on the host* (`/roms` vs `/storage` conventions differ), but the script body is identical.

**One genuinely different procedure:** the first-time credential setup (§7.3, `setup-device.sh`). The GKD has a fixed default password; ROCKNIX regenerates the password on every boot. The script is shared; the *instructions in `--help` and the error messages* are profile-driven.

### 8.3 Bench suite

- **Shared:** every metric, every report format, every mode.
- **Shared but profile-fed:** window size (640×480 on both), sample rate and buffer size, and — critically — **the fileio working directory**. The GKD plan's `bench_fileio` uses `/tmp/spectrax_bench_fileio_XXXXXX`. On a handheld, `/tmp` is commonly a tmpfs, which would measure RAM bandwidth and produce a meaningless "file I/O" number. The device profile must point fileio at the real storage volume.
- **Device-specific bench concern:** the RG35XX SP's panel is 640×480 native, so the bench's gfx mode needs no scaling — the same as the GKD. The *clamshell* adds one bench concern: a bench run interrupted by lid close / fake suspend produces a truncated report. The bench already needs a signal-safe flush (the GKD spec requires it for SIGINT/SIGTERM); the SP adds SIGTERM-from-suspend to the same requirement.

### 8.4 Testing tiers

The GKD spec's ladder is **A** (QEMU user-mode + Xvfb), **B** (A + CPU clamp), **C** (QEMU system + Armbian RK3326 rootfs), **D** (on device).

| Tier | Applies to the RG35XX SP? | Detail |
|---|---|---|
| **A** | **Yes** | `setup-qemu-binfmt.sh` already registers aarch64; the aarch64 bundle (8.1) makes this usable for both devices |
| **B** | **Yes, with a caveat** | `cpu-clamp.sh` clamps the *host*. On the SP the interesting clamp is 4 cores @ 1.5 GHz — the same numbers as the GKD — but the A53 is out-of-order, so a host clamp remains an approximation, not a model |
| **C** | **NO — genuinely device-specific** | The GKD's Tier C boots an Armbian RK3326 rootfs. There is no Armbian H700 image, and qemu-system has no H700 machine model. The closest approximation is `qemu-system-aarch64 -M virt -cpu cortex-a53` with a *mainline* kernel plus the H700 DTB, which is a research project, not a smoke test. **Tier C is skipped for the SP** and this is stated rather than faked |
| **D** | **Yes** | On-device runs, with the transport ladder of §8.2 |
| **E (new)** | **SP only, and cheap** | A "USB gadget enumeration" tier: with the device booted and the cable attached, check that the host sees a `usb0`/`gadget` NIC and that `ssh` reaches `169.254.170.1`. This is a 5-minute test that decides the whole deploy story, so it is worth its own tier |

### 8.5 Error handling

- Profile lint failure → refuse to deploy, print the offending key and the fix.
- Unresolved probes → `deploy-spectrax.sh` refuses to deploy and names the outstanding probe keys; `deploy-sd.sh` warns and proceeds (the SD path is the bootstrap path); the loader itself never refuses, because the probes need to load the profile to find the device.
- Unknown transport requested → fall through to the next transport in `transport.order`, printing which one was used and why the earlier ones were skipped.
- No transport available → print the SD-card instructions (both devices) and exit non-zero.
- Bench never crashes on missing assets (carried over from the GKD spec); signal-safe report flush on SIGINT/SIGTERM (extended to cover suspend-driven SIGTERM on the SP).
- Probe scripts never fail the build; they emit findings and leave the profile's `probes.needed` list updated.

### 8.6 Performance assumptions to measure (not optimise)

- A53's NEON is 128-bit and the core is out-of-order: expect better throughput than the A35 at the same clock. **Do not compare SP numbers to Pixel 2 numbers without saying so.**
- Mali-G31 supports Vulkan 1.0 and, under Panfrost, GLES 3.1 (V7). The bench's GLES2 path is sufficient for the workload.
- 1 GB of RAM caps sample loading and FFT buffers; the bench must not allocate before it is enabled.
- The RG35XX SP's storage is a microSD card; fileio throughput is a property of the *card*, not the SoC. The bench must record the card's identity (or at least its class) or the numbers are not comparable across runs.

---

## 9. Risks / open items — every unknown has a named probe

Each open item names the probe that resolves it and the profile key it writes.

### 9.1 Which OS, and which userspace ABI? → `probe-os.sh`
Resolves `cfw.family`, `device.arch`, `cfw.hostname`, `cfw.password_source`, `cfw.ssh_enabled_by_default`.
Probe: over SSH (or ADB, if A12 turns out to apply) dump `/etc/os-release`, `uname -m`, `uname -n`, `getent passwd root`, the existence and content of `/storage/.config/…` vs `/roms/…`, and whether `sshd` is running.

### 9.2 Does the USB-C port enumerate as a gadget? → `probe-usb.sh`
Resolves `transport.usb_gadget_host_ip` / `usb_gadget_device_ip` and whether `usb-gadget` stays in `transport.order`.
Probe (on the device): `ls /sys/class/udc/`, `cat /sys/class/udc/*/function`, `cat /sys/class/usb_role/*/role`, `ls /sys/class/net/` for a `usb0`/`gadget` interface, `usbgadget` with no arguments (prints the current mode), `usbgadget address`.
Probe (on the host): `ip link` for a new NIC, `ip addr` for `169.254.170.1`, `ssh -o ConnectTimeout=5 root@169.254.170.1 true`.
Also record whether the host's `usb_modeswitch`/RNDIS path is needed (the `usbgadget` source comments say NCM is the default and RNDIS needs `usb_modeswitch -v 1d6b -p 0104 -u 2` on the host).
**This is the highest-value probe in the whole plan.** It decides whether the deploy story is "cable" or "dongle".

### 9.3 Where do ports actually live? → `probe-paths.sh`
Resolves `paths.ports_dir`, `paths.storage_root`.
Probe: `ls -ld /roms /storage /storage/roms /storage/roms/ports /roms/ports`, `readlink -f` on each, `findmnt` / `/proc/mounts` for the relevant mounts, and the presence of `PortMaster`.

### 9.4 What does the display actually do? → `probe-display.sh`
Resolves `display.scale`, `display.native`, `gpu.api`, `gpu.driver`.
Probe: `cat /sys/class/drm/card0/modes`, `dmesg | grep -i -E "drm|panel"`, `eglinfo`/`es2_info` if present, `wlr-randr` or `swaymsg -g outputs` for the compositor's view of the mode, and `glxinfo -B`-equivalent for the GL vendor/renderer string (expect `panfrost`).

### 9.5 What does the audio subsystem look like? → `probe-audio.sh`
Resolves the ALSA card id, device name and native sample rates, and whether the headphone route needs an explicit mixer control.
Probe: `cat /proc/asound/cards`, `aplay -l`, `aplay -L`, `arecord -l`, `amixer -c 0 contents` (the 74HC4052D mux shows up as a mixer control, V24).

### 9.6 What clock does the device actually run at? → `probe-cpu.sh`
Resolves `device.cpu_mhz` and whether the 1.5 GHz "overclock" is a cpufreq OPP.
Probe: `cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_{available_frequencies,driver,governor,max_freq}`, `scaling_cur_freq` under load, `dmesg | grep -i -E "cpufreq|opp|clock"`, and `cat /sys/devices/system/cpu/cpu*/cpufreq/affected_cpus`.

### 9.7 Will raylib get a context? → `probe-gpu.sh`
Resolves `gpu.api` and whether the DRM/EGL raylib build works under Sway.
Probe: run the cross-built `spectrax_bench --bench=gfx` for one frame on the device and capture the raylib log; check for `EGL_PLATFORM=surfaceless`/Wayland environment requirements; `ls /usr/share/egl/egl_external_platforms*`, `ls /usr/lib/*/libgbm*`, `ls /dev/dri`.

### 9.8 Is the RAM DDR3 or LPDDR4? → `probe-mem.sh`
Resolves `device.ram_type` and `device.ram_mb`.
Probe: `cat /proc/meminfo` (total), `dmesg | grep -i -E "dram|memory|ddr"`, `cat /sys/devices/platform/sun50i-h616-ddr*/` if exposed, `lshw -class memory` if present.

### 9.9 How long does it take to boot? → `probe-boot.sh`
Resolves a boot-time budget for the deploy's readiness wait.
Probe: time from power-on to SSH-accepting, measured with a host-side timer around the `discover-device.sh` retry loop; also capture the ES "Network Settings" → "Enable SSH" round trip time, which is a *human* step and therefore the slowest link in the chain.

### 9.10 Does ADB exist? → `probe-os.sh` (extended)
Resolves whether an ADB transport should be added to `transport.order`.
Probe: `command -v adb`, `ls /usr/bin/adb*`, `ps | grep adbd`, `getprop` if present. Only worth chasing if the chosen CFW is the stock Anbernic OS or GarlicOS.

### 9.11 What happens to a running app when the lid closes? → `probe-lid.sh`
Resolves the suspend policy for the bench and the app.
Probe: start a long-running process on the device, close the lid, observe from the host over SSH whether the process survives, whether SSH drops, and what `dmesg` says.

### 9.12 Does the device have enough free RAM for the app + bench? → `probe-mem.sh`
Resolves whether the bench must run with a reduced voice count.
Probe: `free -m` with the frontend running; note ROCKNIX's own footprint.

### 9.13 Will the SD card path work? → `probe-storage.sh`
Resolves the write throughput reality and whether `paths.data_dir` needs a specific partition.
Probe: `findmnt`, `df -h /storage`, a 64 MB write test with `dd`, and the card's reported model.

### 9.14 PortMaster convention on ROCKNIX → `probe-paths.sh` (extended)
Resolves whether our port must live under PortMaster's own directory and be launched by it, or can be launched directly by EmulationStation.
Probe: `ls /storage/roms/ports`, `ls /storage/.config/PortMaster` if present, and an ES "Ports" menu entry.

---

## 10. How this document changes the GKD plan's interpretation

This is the part the GKD plan's author needs to read first.

1. **The GKD plan is not "the GKD device plan" any more — it is "the shared rig plan, with the GKD as its first profile".** Every script it writes must take its device-specific values from a profile rather than hardcoding `rogue_pixel2`, `root`, `rogue`, `/roms/ports`, `setgpu.sh panfrost` and `-mcpu=cortex-a35`.
2. **Its Task 1 is incomplete.** Writing two cross files is not cross-compiling; producing the **aarch64 lib bundle** (raylib with `PLATFORM_DRM`/GLES2, PortAudio with ALSA, plus a per-arch `libdir`) is the real task, and it is shared. The GKD plan's assumption that meson "just works" with the vendored `lib/linux` raylib is false: `meson.build` hardcodes `lib/linux`, and there is no aarch64 `libraylib.a` anywhere in the repo.
3. **Its Task 9's GPU-switch helper (`/usr/bin/setgpu.sh panfrost`) is a guess and should become a profile field**, probed rather than assumed. On the RG35XX SP the driver is Panfrost by default and no switch helper is known.
4. **Its Task 10's `deploy-spectrax.sh` gains a transport ladder** rather than a single WiFi-SSH path.
5. **Its Task 11's `setup-pixel.sh` credential flow is not portable as written**: ROCKNIX regenerates the root password on every reboot. The shared script needs a `password_source` concept, and the GKD profile pins `fixed`/`rogue`.
6. **Its Tier C is GKD-only.** A "system emulation" tier for the H700 would need a mainline kernel + DTB under `qemu-system -M virt`; that is a research project and is out of scope. The SP relies on Tiers A/B/D plus the new USB-gadget tier.
7. **Its `bench_fileio` working directory must not be `/tmp`** when run on a device, or the fileio numbers describe tmpfs rather than the SD card.
8. **The GKD branch must gain a `devices/gkd-pixel2.toml` profile and be retrofitted onto the shared scripts** — an explicit task, so the GKD effort is not orphaned. The retrofit is sequenced *after* the shared generalisation and *before* the RG35XX-SP-specific work, so the GKD device keeps working at every phase boundary.

## 11. Out of scope

- Choosing or flashing a CFW for the user.
- Making the app itself clamshell-aware beyond documenting the behaviour (that is app work, not tooling).
- A third device profile.
- Miyoo Mini / Mini Plus support (different panel geometry — 320×240 and 640×360 — so the "640×480 fits natively" property would not hold).
- Bench history dashboard, CI integration, run-on-save automation — deferred, as in the GKD spec.
- Android/ADB transports, unless `probe-os.sh` shows the user's chosen CFW exposes them.

---

## 12. Implementation status (2026-10-22)

The tooling described above is implemented on branch `rg35xxsp-tooling`.
This section records what was established on the host, what is still open,
and where the open items stand.

### 12.1 Resolved on the host

| Item | Status | Evidence |
|------|--------|----------|
| The missing aarch64 lib bundle (R4, §8.1) | **resolved** | `lib/aarch64/` carries raylib 5.5 (`PLATFORM_DRM` + `GRAPHICS_API_OPENGL_ES2`), PortAudio 19.7.0 with the ALSA backend, kissfft, and the target headers. `scripts/build-target-libs.sh aarch64` rebuilds it. |
| The hardcoded `lib/linux` (R5) | **resolved** | `meson.build` reads `spectrax_libdir` and `spectrax_incdir` from the cross file; the native build still defaults to `lib/linux`. |
| Cross-compiling for both devices | **resolved** | `build-gkd-pixel2` and `build-rg35xxsp` both configure and compile clean with `cortex-a35` / `cortex-a53` respectively. The app's `DT_NEEDED` is exactly the device's GL stack: `libEGL.so.1`, `libGLESv2.so.2`, `libgbm.so.1`, `libdrm.so.2`, `libasound.so.2`. |
| Tier A (QEMU user-mode) | **resolved** | `meson test -C build test_qemu_smoke` cross-builds and runs `test_voice` (11 tests) and `spectrax_bench` under `qemu-aarch64-static`. |
| Tier B (CPU clamp) | **not applicable as written** | `cpu-clamp.sh` clamps the *host*; the A53 is out-of-order, so a host clamp is an approximation, not a model (unchanged from the spec's §8.4). |
| Tier C (system emulation) | **skipped, with a reason** | There is no Armbian H700 image and QEMU has no H700 machine model. The GKD keeps its RK3326 helpers under `scripts/devices/gkd-pixel2/`; the SP relies on Tiers A/B/D plus the USB-gadget tier. |
| The device as data | **resolved** | Two profiles (`devices/gkd-pixel2.toml`, `devices/rg35xxsp.toml`), one loader, one linter, one cross-file generator, one deploy ladder, one bench. The two profiles differ in 18 leaf values; no script is forked per device. |

### 12.2 The library bundle: what ships and what the device provides

The target's EGL/GLES2/GBM/DRM/ALSA libraries live in the device's
userspace. A cross build cannot link against them (the cross sysroot has no
aarch64 copy) and shipping foreign distro builds of them would be worse than
not shipping them at all. So `lib/aarch64/stub/` carries **link-time
stubs**: shared objects with the right SONAME and the right exported symbol
names, but no code. The link is satisfied; at run time the loader resolves
the symbols from the device's real libraries, because the loader looks the
SONAME up, not the file.

The same stubs are what makes the QEMU smoke test possible: the guest
sysroot is the cross toolchain's loader and libc plus the stubs.

### 12.3 Open items (device required)

None of these can be closed without the hardware. Each still names its probe.

| § | Open item | Probe | State |
|---|-----------|-------|-------|
| 9.1 | Which OS, which userspace ABI | `probe-os.sh` | open — the profile assumes ROCKNIX aarch64 (V8) |
| 9.2 | Does the USB-C port enumerate as a gadget | `probe-usb.sh` | open — **the highest-value probe**; `scripts/usb-gadget.sh` reports the link state |
| 9.3 | Where do ports live | `probe-paths.sh` | open — the profile assumes `/storage/roms/ports` (V19/V20) |
| 9.4 | What the display does | `probe-display.sh` | open — the panel is 640×480 (V9) |
| 9.5 | The audio subsystem | `probe-audio.sh` | open — the codec's analog mux is in the device tree (V24) |
| 9.6 | The actual clock | `probe-cpu.sh` | open — 1.4 GHz stock, 1.5 GHz with the ES overclock (V5) |
| 9.7 | Will raylib get a context | `probe-gpu.sh` + a one-frame gfx run | open — the DRM/EGL/GLES2 build is in place; the device's mesa is not |
| 9.8 | DDR3 or LPDDR4 | `probe-mem.sh` | open — the two vendor sources still disagree (V10/A10) |
| 9.9 | Boot time | `probe-boot.sh` | open |
| 9.10 | ADB | `probe-os.sh` | open, and not needed for ROCKNIX |
| 9.11 | What the lid does to a running app | `probe-lid.sh` | open — the bench flushes on SIGHUP/SIGTERM, so an interrupted run still leaves a report |
| 9.12 | Free RAM headroom | `probe-mem.sh` | open |
| 9.13 | The SD card path | `probe-storage.sh` | open |
| 9.14 | The PortMaster convention | `probe-paths.sh` | open |

### 12.4 The bench on a device

`bench_fileio` defaults to the profile's `paths.data_dir`, not `/tmp`: on a
handheld `/tmp` is commonly a tmpfs, and the numbers would describe RAM
rather than the SD card. `--workdir` overrides it, and the report records
the directory the numbers actually describe.

`bench_gfx` uses the profile's panel size, and the bench never spawns an X
server when `--device` is given: a device has a real panel.
