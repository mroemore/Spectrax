# Tests: one executable per dsp/ test file, all linking the core object set.
# test_sequencer additionally links seq_lib (it stubs gui's rebuildPatternGraph,
# which appstate.o references). Registered with meson's test() runner.

test_names = [
  'test_io',
  'test_voice',
  'test_modsystem',
  'test_mod_voice',
  'test_graph_nav',
  'test_cfg',
  'test_input',
  'test_layout',
  'test_filters',
  'test_gui_layer',
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
  link_libs = [core_lib]
  if name == 'test_graph_nav'
    link_libs = test_graph_nav_libs
  elif name == 'test_mod_voice'
    link_libs = test_mod_voice_libs
  elif name == 'test_voice'
    link_libs = [core_lib, seq_lib]
  elif name == 'test_cfg'
    link_libs = test_cfg_libs
  elif name == 'test_layout'
    link_libs = test_layout_libs
  elif name == 'test_gui_layer'
    link_libs = gui_test_libs
  endif
  exe = executable(name,
    files('dsp' / name + '.c'),
    extra_srcs,
    link_with: link_libs,
    include_directories: inc,
    dependencies: system_deps,
  )
  test(name, exe)
endforeach

exe = executable('test_sequencer',
  files('dsp/test_sequencer.c'),
  link_with: [core_lib, seq_lib],
  include_directories: inc,
  dependencies: system_deps,
)
test('test_sequencer', exe)

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