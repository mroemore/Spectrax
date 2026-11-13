# Tests: one executable per dsp/ test file, all linking the core object set.
# test_sequencer additionally links seq_lib (it stubs gui's rebuildPatternGraph,
# which appstate.o references). Registered with meson's test() runner.

test_names = [
  'test_io',
  'test_voice',
  'test_audio_concurrency',
  'test_modsystem',
  'test_mod_voice',
  'test_graph_nav',
  'test_cfg',
  'test_input',
  'test_layout',
  'test_theme',
  'test_waveform',
  'test_engine_rate',
  'test_canonical_math',
  'test_sampler_character',
  'test_sampler_filter',
  'test_sampler_crossfade',
  'test_gui_sampler_points',
  'test_golden_render',
  'test_voice_isolation',
  'test_device_budget',
  'test_device_calibrate',
  'test_voice_governor',
  'test_note_velocity',
  'test_sample_pool',
  'test_audio_device',
  'test_filters',
  'test_gui_layer',
  'test_gui_pattern',
  'test_gui_arranger',
  'test_gui_instrument',
  'test_gui_inst_mod',
  'test_gui_inst_mod_mirror',
  'test_gui_mod_screen',
  'test_modstrip_lazy',
  'test_gui_arranger_fault',
  'test_bench_report',
]

# The GUI/UI sources now live in src/libspectrax_gui.a (see src/meson.build,
# TESTING.md I27). Tests that include gui.h link gui_lib instead of recompiling
# ../src/gui_*.c: one compilation, normal object names, coverage lcov can read.
gui_test_libs = [core_lib, seq_lib, vizfx_lib, gui_lib]

# Task 3: gui.c hosts the instrument chip node + creator + recogniser.
# test_graph_nav exercises the chip row layout, so it must link the GUI lib.
test_graph_nav_libs = gui_test_libs

# Task 2 (chip-palette theme keys): test_cfg's test_chip_palette_theme_roundtrip
# calls initDefaultColourScheme, which lives in gui_core.c. Same link surface.
test_cfg_libs = test_graph_nav_libs

# Task 5: test_mod_voice covers the chip label-edit char-cycle + cursor pure
# helpers in gui.c. Same link surface.
test_mod_voice_libs = gui_test_libs

# Task 3: test_layout's step-cell test calls initDefaultColourScheme. Same
# link surface as test_cfg / test_graph_nav.
test_layout_libs = test_graph_nav_libs

foreach name : test_names
  extra_srcs = []
  test_c_args = []
  extra_incs = []
  extra_link_args = []
  link_libs = [core_lib]
  if name == 'test_graph_nav'
    link_libs = test_graph_nav_libs
  elif name == 'test_mod_voice'
    link_libs = test_mod_voice_libs
  elif name == 'test_voice'
    link_libs = gui_test_libs
  elif name == 'test_io'
    # The I32 regressions drive the loaded project through createSequencer /
    # startPlaying / advanceSequencerStep, which live in seq_lib.
    link_libs = gui_test_libs
  elif name == 'test_audio_concurrency'
    # audio.c lives in core_lib precisely so this can link it; it also needs
    # seq_lib for advanceSequencerStep and the GUI lib for the draw helpers
    # the arranger pulls in transitively.
    link_libs = [core_lib, seq_lib, gui_lib, vizfx_lib]
  elif name == 'test_cfg'
    link_libs = test_cfg_libs
  elif name == 'test_layout'
    link_libs = test_layout_libs
    # test_layout validates the SHIPPED config, not a fixture: compile the
    # repo path in so the test can never drift onto a stale copy.
    test_c_args = ['-DSPECTRAX_SRC_LAYOUT_JSON="' + meson.project_source_root() + '/src/layout.json"']
  elif name == 'test_audio_device'
    # The stream's opening: PortAudio.
    link_libs = [core_lib]
    link_with = [portaudio]
  elif name == 'test_sample_pool'
    # The sample pool is a leaf module.
    link_libs = [core_lib]
  elif name == 'test_note_velocity'
    # The velocity reaches the voice through the sequencer and the file.
    link_libs = [core_lib, seq_lib, gui_lib, vizfx_lib]
  elif name == 'test_voice_governor'
    # The governor is a standalone policy module.
    link_libs = [core_lib]
  elif name == 'test_device_calibrate'
    # The calibration renders a voice, so it needs the voice manager and
    # the preset bank.
    link_libs = [core_lib]
  elif name == 'test_device_budget'
    # The budget's profile is written with cjson.
    link_libs = [core_lib]
  elif name == 'test_voice_isolation'
    # The voices live in the voice manager, and the sequencer's helpers are
    # pulled in transitively.
    link_libs = [core_lib, seq_lib, gui_lib, vizfx_lib]
  elif name == 'test_golden_render'
    # The recorded hash lives in the repo, so the test needs the root.
    test_c_args = ['-DSPECTRAX_REPO_ROOT="' + meson.project_source_root() + '"']
    # The callback render needs the same surface as the concurrency test:
    # seq_lib for the sequencer, the GUI lib for the draw helpers the
    # arranger pulls in transitively, and vizfx for the scroller and the
    # mix ring the callback pushes into.
    link_libs = [core_lib, seq_lib, gui_lib, vizfx_lib]
  elif name == 'test_sampler_crossfade'
    # The no-libm lint greps the crossfade's source, so it needs its path.
    test_c_args = ['-DSPECTRAX_SRC_DIR="' + meson.project_source_root() + '/src"']
  elif name == 'test_sampler_filter'
    # The no-libm lint greps the cascade's source, so it needs its path.
    test_c_args = ['-DSPECTRAX_SRC_DIR="' + meson.project_source_root() + '/src"']
  elif name == 'test_canonical_math'
    # The contract test greps the canonical sources, so it needs their path.
    test_c_args = ['-DSPECTRAX_SRC_DIR="' + meson.project_source_root() + '/src"']
  elif name == 'test_engine_rate'
    # The contract test greps the synthesis sources, so it needs their path.
    test_c_args = ['-DSPECTRAX_SRC_DIR="' + meson.project_source_root() + '/src"']
    # createArranger lives in seq_lib; appstate.o's rebuildPatternGraph is
    # stubbed in the test file.
    link_libs = [core_lib, seq_lib]
  elif name == 'test_theme'
    # test_theme validates the SHIPPED theme for the same reason.
    test_c_args = ['-DSPECTRAX_THEME_JSON="' + meson.project_source_root() + '/bin/clr.json"']
  elif name == 'test_gui_layer'
    link_libs = gui_test_libs
  elif name == 'test_gui_pattern'
    link_libs = gui_test_libs
  elif name == 'test_gui_arranger'
    link_libs = gui_test_libs
  elif name == 'test_gui_instrument'
    link_libs = gui_test_libs
  elif name == 'test_gui_inst_mod'
    link_libs = gui_test_libs
  elif name == 'test_gui_inst_mod_mirror'
    link_libs = gui_test_libs
  elif name == 'test_gui_inst_mod_picker'
    link_libs = gui_test_libs
  elif name == 'test_gui_mod_screen'
    link_libs = gui_test_libs
  elif name == 'test_modstrip_lazy'
    # gui_test_libs, not bare vizfx: vizfx.c calls getColourScheme(), which
    # lives in gui_core.c.
    link_libs = gui_test_libs
  elif name == 'test_bench_report'
    # The report writer is bench code, compiled into the test directly.
    extra_srcs = files('../src/tools/bench/bench_report.c')
    extra_incs = [inc, cjson_inc, include_directories('../src/tools/bench')]
  elif name == 'test_gui_arranger_fault'
    # --wrap=malloc so createInstChipGuiNode / createArrangerCellGuiNode's OOM
    # guards are reachable. gui_lib leads because gui_arranger.o references
    # symbols in core_lib (initGuiNode) and seq_lib.
    extra_srcs = files('support/faultinject.c')
    extra_incs = [inc, include_directories('support')]
    link_libs = [gui_lib, core_lib, seq_lib, vizfx_lib]
    # faultinject.c always references the fopen/fread/fclose real symbols, so
    # the full wrap set must be present even though this target only fails
    # allocations.
    extra_link_args = ['-Wl,--wrap=malloc', '-Wl,--wrap=calloc',
                       '-Wl,--wrap=realloc', '-Wl,--wrap=free',
                       '-Wl,--wrap=fopen', '-Wl,--wrap=fread',
                       '-Wl,--wrap=fclose', '-Wl,--wrap=fopen64']
  endif
  exe = executable(name,
    files('dsp' / name + '.c'),
    extra_srcs,
    link_with: link_libs,
    include_directories: extra_incs == [] ? [inc] : extra_incs,
    link_args: extra_link_args,
    dependencies: system_deps,
    c_args: test_c_args,
  )
  # The race suite: the tests that exercise real threads. The race
  # detectors run this suite only (meson/matrix.sh), and they CHECK the
  # declaration - every test whose source creates a thread must be in it,
  # or the race detector would not cover it and nothing would say so.
  test_suites = []
  if name == 'test_audio_concurrency'
    test_suites = ['race']
  endif
  test(name, exe, suite: test_suites)
endforeach

exe = executable('test_sequencer',
  files('dsp/test_sequencer.c'),
  link_with: [core_lib, seq_lib],
  include_directories: inc,
  dependencies: system_deps,
)
test('test_sequencer', exe)

# Device-profile loader/linter contract (host-side, no device needed).
test('test_device_profile',
  find_program('dsp/test_device_profile.sh'),
  args: [],
)

# Cross-compile + qemu-user smoke test. The first run cross-builds the
# device target, so it gets a generous timeout.
test('test_qemu_smoke',
  find_program('dsp/test_qemu_smoke.sh'),
  args: [],
  suite: ['device'],
  timeout: 900,
)

# vizfx tests are standalone (pure buffer-scroller data ops).
exe = executable('test_vizfx',
  files('dsp/test_vizfx.c'),
  link_with: [core_lib, vizfx_lib],
  include_directories: inc,
  dependencies: system_deps,
)
test('test_vizfx', exe)

# Contract-macro self-test (src/verify.h). Pure: links core_lib only for the
# spxCoverage hook that testcase() references in coverage builds.
exe = executable('test_verify',
  files('dsp/test_verify.c'),
  link_with: [core_lib],
  include_directories: inc,
  dependencies: [m],
)
test('test_verify', exe)

# Pure-module boundary suites: notes (string formatting) and wavetable
# (fixed-arena packing + capacity edges).
exe = executable('test_notes',
  files('dsp/test_notes.c'),
  link_with: [core_lib],
  include_directories: inc,
  dependencies: [m],
)
test('test_notes', exe)

exe = executable('test_wavetable',
  files('dsp/test_wavetable.c'),
  link_with: [core_lib],
  include_directories: inc,
  dependencies: [m],
)
test('test_wavetable', exe)

# OOM + I/O fault injection: allocation and stdio calls in the linked object are
# redirected through tests/support/faultinject.c via the linker's --wrap
# mechanism. (Mutation testing is separate; see test_mutate_input below.)
exe = executable('test_wavetable_fault',
  files('dsp/test_wavetable_fault.c', 'support/faultinject.c'),
  link_with: [core_lib],
  include_directories: [inc, include_directories('support')],
  dependencies: [m],
  link_args: [
    '-Wl,--wrap=malloc',
    '-Wl,--wrap=calloc',
    '-Wl,--wrap=realloc',
    '-Wl,--wrap=free',
    '-Wl,--wrap=fopen',
    '-Wl,--wrap=fread',
    '-Wl,--wrap=fclose',
    '-Wl,--wrap=fopen64',
  ],
)
test('test_wavetable_fault', exe)

# I/O + OOM fault injection for the WAV loader (fopen/fread and the allocator).
exe = executable('test_io_fault',
  files('dsp/test_io_fault.c', 'support/faultinject.c'),
  link_with: [core_lib],
  include_directories: [inc, include_directories('support')],
  dependencies: [m],
  link_args: [
    '-Wl,--wrap=malloc',
    '-Wl,--wrap=calloc',
    '-Wl,--wrap=realloc',
    '-Wl,--wrap=free',
    '-Wl,--wrap=fopen',
    '-Wl,--wrap=fread',
    '-Wl,--wrap=fclose',
    '-Wl,--wrap=fopen64',
  ],
)
test('test_io_fault', exe)

# Allocation-failure paths for gui_layer.c (pushLayer growth, createLayer).
exe = executable('test_gui_layer_fault',
  files('dsp/test_gui_layer_fault.c', 'support/faultinject.c'),
  link_with: gui_test_libs,
  include_directories: [inc, include_directories('support')],
  dependencies: [m],
  link_args: [
    '-Wl,--wrap=malloc',
    '-Wl,--wrap=calloc',
    '-Wl,--wrap=realloc',
    '-Wl,--wrap=free',
    '-Wl,--wrap=fopen',
    '-Wl,--wrap=fread',
    '-Wl,--wrap=fclose',
    '-Wl,--wrap=fopen64',
  ],
)
test('test_gui_layer_fault', exe)

# layerStackDraw is the one gui_layer.c function with real raylib draw calls.
# Run it under Xvfb (a real GL context), matching the vizulobe GL tests.
xvfb = find_program('xvfb-run', required: false)
if xvfb.found()
  exe = executable('test_gui_layer_draw',
    files('dsp/test_gui_layer_draw.c'),
    link_with: gui_test_libs,
    include_directories: inc,
    dependencies: system_deps,
  )
  test('test_gui_layer_draw', xvfb,
    args: ['-a', '-s', '-screen 0 640x480x24', exe],
    timeout: 60,
  )

  # I/O + allocation failure injection for the two binary loaders
# (io/sequencer_io.c, io/preset_io.c) -- see TESTING.md I33.
  exe = executable('test_io_fault2',
    files('dsp/test_io_fault2.c', 'support/faultinject.c'),
    link_with: gui_test_libs,
    include_directories: [inc, include_directories('support')],
    dependencies: [m],
    link_args: [
      '-Wl,--wrap=malloc',
      '-Wl,--wrap=calloc',
      '-Wl,--wrap=realloc',
      '-Wl,--wrap=free',
      '-Wl,--wrap=fopen',
      '-Wl,--wrap=fread',
      '-Wl,--wrap=fclose',
      '-Wl,--wrap=fopen64',
    ],
  )
  test('test_io_fault2', exe)

  # Allocation-failure paths for gui_pattern.c (createSongMinimapGui).
  exe = executable('test_gui_pattern_fault',
    files('dsp/test_gui_pattern_fault.c', 'support/faultinject.c'),
    link_with: gui_test_libs,
    include_directories: [inc, include_directories('support')],
    dependencies: [m],
    link_args: [
      '-Wl,--wrap=malloc',
      '-Wl,--wrap=calloc',
      '-Wl,--wrap=realloc',
      '-Wl,--wrap=free',
      '-Wl,--wrap=fopen',
      '-Wl,--wrap=fread',
      '-Wl,--wrap=fclose',
      '-Wl,--wrap=fopen64',
    ],
  )
  test('test_gui_pattern_fault', exe)

  # The lazy half of I41: ModStrip allocates its ping/pong pair on first use.
  exe = executable('test_modstrip_lazy_draw',
    files('dsp/test_modstrip_lazy_draw.c'),
    link_with: gui_test_libs,
    include_directories: inc,
    dependencies: system_deps,
  )
  test('test_modstrip_lazy_draw', xvfb,
    args: ['-a', '-s', '-screen 0 640x480x24', exe],
    timeout: 60,
  )

  # gui_arranger.c's cell + chip draw callbacks.
  exe = executable('test_gui_arranger_draw',
    files('dsp/test_gui_arranger_draw.c'),
    link_with: gui_test_libs,
    include_directories: inc,
    dependencies: system_deps,
  )
  test('test_gui_arranger_draw', xvfb,
    args: ['-a', '-s', '-screen 0 640x480x24', exe],
    timeout: 60,
  )

  # Same for gui_pattern.c's draw callbacks (step cells + song minimap).
  exe = executable('test_gui_pattern_draw',
    files('dsp/test_gui_pattern_draw.c'),
    link_with: gui_test_libs,
    include_directories: inc,
    dependencies: system_deps,
  )
  test('test_gui_pattern_draw', xvfb,
    args: ['-a', '-s', '-screen 0 640x480x24', exe],
    timeout: 60,
  )
endif

# Input-byte mutation testing (P7): flips seeded bits of a valid WAV and
# requires the loader's assertions to notice. No --wrap: it perturbs the file,
# not the allocator.
exe = executable('test_mutate_input',
  files('dsp/test_mutate_input.c', 'support/mutate.c'),
  link_with: [core_lib],
  include_directories: [inc, include_directories('support')],
  dependencies: [m],
  c_args: ['-DSPX_MUTATE'],
)
test('test_mutate_input', exe)

# Allocation-failure paths for filters.c (createBiquadFilter / createFilter)
# via the same --wrap harness: the only way to reach the !bf / !flt branches.
exe = executable('test_filters_fault',
  files('dsp/test_filters_fault.c', 'support/faultinject.c'),
  link_with: [core_lib],
  include_directories: [inc, include_directories('support')],
  dependencies: [m],
  link_args: [
    '-Wl,--wrap=malloc',
    '-Wl,--wrap=calloc',
    '-Wl,--wrap=realloc',
    '-Wl,--wrap=free',
    '-Wl,--wrap=fopen',
    '-Wl,--wrap=fread',
    '-Wl,--wrap=fclose',
    '-Wl,--wrap=fopen64',
  ],
)
test('test_filters_fault', exe)
