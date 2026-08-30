# App + shared sources. The app is main.c + gui.c + distortion + dataviz on top
# of the core/seq object sets. The tests link core (and seq for test_sequencer)
# so appstate.o's gui dependency (rebuildPatternGraph) is never pulled into
# tests that don't stub it.

# Sources shared by the app and the dsp/UI tests (the old Makefile.test APP_OBJS).
core_sources = files(
  'voice.c',
  'modsystem.c',
  'oscillator.c',
  'wavetable.c',
  'dstruct.c',
  'blit_synth.c',
  'filters.c',
  'fft.c',
  'notes.c',
  'sample.c',
  'graph_gui.c',
  'io.c',
  'io/gui_io.c',
  'io/preset_io.c',
  'io/sequencer_io.c',
  'io/settings_io.c',
  'io/config_io.c',
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

# App-only sources (main carries the audio/init glue; gui owns the graph UI).
app_only_sources = files(
  'main.c',
  'gui.c',
  'gui_layer.c',
  'distortion.c',
  'dataviz.c',
)

# Buffer-scroller / mod-strip visualisations: used by the app (audio callback,
# gui graph nodes) and unit-tested by test_vizfx.
vizfx_lib = static_library('spectrax_vizfx', 'vizfx.c',
  include_directories: inc,
)

core_lib = static_library('spectrax_core', core_sources,
  include_directories: inc,
  dependencies: [m],
)

seq_lib = static_library('spectrax_seq', seq_sources,
  include_directories: inc,
  dependencies: [m],
)

spectrax = executable('spectrax',
  app_only_sources,
  link_with: [core_lib, seq_lib, vizfx_lib],
  include_directories: inc,
  dependencies: system_deps,
)

# Used by the harness targets below.
core_dep = declare_dependency(link_with: core_lib, include_directories: inc)
seq_dep = declare_dependency(link_with: seq_lib, include_directories: inc)