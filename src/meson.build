# App + shared sources. The app is main.c + gui.c + distortion + dataviz on top
# of the core/seq object sets. The tests link core (and seq for test_sequencer)
# so appstate.o's gui dependency (rebuildPatternGraph) is never pulled into
# tests that don't stub it.

# Sources shared by the app and the dsp/UI tests (the old Makefile.test APP_OBJS).
core_sources = files(
  'voice.c',
  'audio.c',
  'dataviz.c',
  'modsystem.c',
  'oscillator.c',
  'sintab.c',
  'canonical_math.c',
  'devicerate.c',
  'wavetable.c',
  'dstruct.c',
  'blit_synth.c',
  'filters.c',
  'fft.c',
  'notes.c',
  'sample.c',
  'sampler_character.c',
  'device_budget.c',
  'device_calibrate.c',
  'graph_gui.c',
  'gui_style.c',
  'io.c',
  'io/gui_io.c',
  'io/preset_io.c',
  'io/sequencer_io.c',
  'io/settings_io.c',
  'io/config_io.c',
  'paths.c',
  'verify.c',
)

# Sequencer/appstate/input/settings: needed by the app and by test_sequencer
# (which stubs gui's rebuildPatternGraph). Kept separate so the other tests
# don't have to carry appstate's gui dependency.
seq_sources = files(
  'sequencer.c',
  'appstate.c',
  'input.c',
  'settings.c',
)

# App-only sources (main carries the init/main-loop glue; audio owns the
# real-time render path; gui owns the graph UI).
# audio.c is deliberately NOT app-only: it lives in core_lib so the
# concurrency tests can link and drive renderAudioBuffer() (see audio.h).
# dataviz.c came with it -- renderAudioBuffer pushes a CPU-time measurement
# into its TimeGraph, so audio.c genuinely depends on it.
app_only_sources = files(
  'main.c',
  'distortion.c',
)

# The instrument-chip UI sources. These used to be recompiled per test from
# ../src/, which made meson name the objects ".._src_gui_core.c.o" — a leading
# ".." that lcov's geninfo silently treats as a hidden file and drops, so the
# GUI carried zero measured coverage. Hoisting them into one static library
# compiles them once, under a normal object name, and both the app and the GUI
# tests link it. See TESTING.md I27.
gui_sources = files(
  'gui_core.c',
  'gui_arranger.c',
  'gui_pattern.c',
  'gui_instrument.c',
  'gui_inst_fm.c',
  'gui_inst_sample.c',
  'gui_inst_mod.c',
  'gui_inst_blep.c',
  'gui_layer.c',
  'curve_icons.c',
)

# Buffer-scroller / mod-strip visualisations: used by the app (audio callback,
# gui graph nodes) and unit-tested by test_vizfx.
vizfx_lib = static_library('spectrax_vizfx', 'vizfx.c',
  include_directories: inc,
)

core_lib = static_library('spectrax_core', core_sources,
  include_directories: [inc, cjson_inc],
  dependencies: [m],
)

seq_lib = static_library('spectrax_seq', seq_sources,
  include_directories: inc,
  dependencies: [m],
)

gui_lib = static_library('spectrax_gui', gui_sources,
  include_directories: [inc, cjson_inc],
  dependencies: system_deps,
)

spectrax = executable('spectrax',
  app_only_sources,
  link_with: [core_lib, seq_lib, vizfx_lib, gui_lib],
  include_directories: inc,
  dependencies: system_deps,
)

# Used by the harness targets below.
core_dep = declare_dependency(link_with: core_lib, include_directories: inc)
seq_dep = declare_dependency(link_with: seq_lib, include_directories: inc)
gui_dep = declare_dependency(link_with: gui_lib, include_directories: inc)
