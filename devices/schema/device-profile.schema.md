# Device profile schema

A device profile is a TOML file under `devices/`. The host-local override
`~/.config/spectrax/devices/<id>.toml` is merged on top, key by key, and is
never committed.

Required sections and keys:

## [device]
id         string   slug; must equal the file stem
vendor     string
model      string
soc        string
arch       string   aarch64 | armhf
cpu_core   string   e.g. cortex-a53
cpu_mhz    int
cores      int
gpu        string
ram_mb     int
ram_type   string
storage    string

## [cfw]
family          string   rocknix | rogue | garlic | knulli | minui | anbernic-stock
hostname        string
user            string
password        string   may be empty
password_source string   fixed | user-set | device-display
ssh_enabled_by_default  bool

## [display]
width   int
height  int
scale   float
native  bool

## [gpu]
driver      string   panfrost | libmali | lima
api         string   gles2 | gles3
helper      string   optional GPU-switch helper path; may be empty
compositor  string   sway | x11
platform    string   raylib PLATFORM_ suffix, e.g. drm

## [audio]
device              string  ALSA/PortAudio device substring; empty = the default device
sample_rate         int     the rate the device must run at. The engine's rate is a
                              compile-time constant (SPECTRAX_ENGINE_RATE, 48000); a
                              device that cannot run at that rate is rejected at the
                              stream open rather than silently substituted, because a
                              substituted rate puts every note at the wrong pitch.
periods_max         int     the device's maximum period count (0 = unknown)
engine_follows_device_rate bool  always false: the engine's rate is a constant, never
                              a property of the device.

## [paths]
storage_root string
ports_dir    string
data_dir     string
runs_dir     string

## [transport]
order                        array of strings; subset of usb-gadget, wifi-ssh, sd
mdns_names                   array of strings
usb_gadget_host_ip           string
usb_gadget_device_ip         string

## [build]
cross_file string   repo-relative path to the arch cross template
libdir     string   repo-relative target lib directory
incdir     string   repo-relative target include directory
c_args     array of strings

## [probes]
needed  array of strings   profile keys still awaiting a probe

Optional keys (never required; the host-local override may set them):

## [device]
ip         string   last known address of the device

## [transport]
last_ip    string   last known address of the device

## [build]
build_dir  string   local override for the build directory

The linter rejects: unknown top-level sections, unknown keys inside a known
section, a missing required key, an empty `device.id`, `device.arch` not in
{aarch64, armhf}, `transport.order` containing an unknown transport, a
`transport.order` entry of `usb-gadget` with no `usb_gadget_host_ip`, and a
`[probes] needed` list that is non-empty when `--strict` is passed.
