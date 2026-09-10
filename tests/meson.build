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