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
]

# Task 3: gui.c hosts the instrument chip node + creator + recogniser.
# test_graph_nav exercises the chip row layout, so it must link gui.o
# + gui_layer.o (which gui.c references). Other tests don't need this.
test_graph_nav_extra = files('../src/gui_core.c', '../src/gui_arranger.c', '../src/gui_pattern.c', '../src/gui_instrument.c', '../src/gui_inst_fm.c', '../src/gui_inst_sample.c', '../src/gui_inst_mod.c', '../src/gui_inst_blep.c', '../src/gui_layer.c')

# Task 2 (chip-palette theme keys): test_cfg's test_chip_palette_theme_roundtrip
# calls initDefaultColourScheme, which lives in gui_core.c (not part of core_lib).
# gui.h pulls in the same surface as test_graph_nav, so link the same gui sources.
test_cfg_extra = test_graph_nav_extra

# Task 5: test_mod_voice covers the chip label-edit char-cycle + cursor
# pure helpers in gui.c (chipLabelCycleCharAt / chipLabelCursorMove /
# chipLabelCharIndex). It needs gui.o + system_deps (raylib, etc.) for
# the helpers to link — gui.h pulls in raylib transitively, so any
# test that includes gui.h has to link the same surface as test_graph_nav.
test_mod_voice_extra = files('../src/gui_core.c', '../src/gui_arranger.c', '../src/gui_pattern.c', '../src/gui_instrument.c', '../src/gui_inst_fm.c', '../src/gui_inst_sample.c', '../src/gui_inst_mod.c', '../src/gui_inst_blep.c', '../src/gui_layer.c')

# Task 3: test_layout's step-cell test calls initDefaultColourScheme to
# populate a real ColourScheme for the baked-default assertions; that
# symbol lives in gui_core.c. Same link surface as test_cfg / test_graph_nav.
test_layout_extra = test_graph_nav_extra

foreach name : test_names
  extra_srcs = []
  link_libs = [core_lib]
  if name == 'test_graph_nav'
    extra_srcs = test_graph_nav_extra
    link_libs = [core_lib, seq_lib, vizfx_lib]
  elif name == 'test_mod_voice'
    extra_srcs = test_mod_voice_extra
    link_libs = [core_lib, seq_lib, vizfx_lib]
  elif name == 'test_voice'
    link_libs = [core_lib, seq_lib]
  elif name == 'test_cfg'
    extra_srcs = test_cfg_extra
    link_libs = [core_lib, seq_lib, vizfx_lib]
  elif name == 'test_layout'
    extra_srcs = test_layout_extra
    link_libs = [core_lib, seq_lib, vizfx_lib]
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