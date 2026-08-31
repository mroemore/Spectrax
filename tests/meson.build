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
]

# Task 3: gui.c hosts the instrument chip node + creator + recogniser.
# test_graph_nav exercises the chip row layout, so it must link gui.o
# + gui_layer.o (which gui.c references). Other tests don't need this.
test_graph_nav_extra = files('../src/gui.c', '../src/gui_layer.c')

# Task 5: test_mod_voice covers the chip label-edit char-cycle + cursor
# pure helpers in gui.c (chipLabelCycleCharAt / chipLabelCursorMove /
# chipLabelCharIndex). It needs gui.o + system_deps (raylib, etc.) for
# the helpers to link — gui.h pulls in raylib transitively, so any
# test that includes gui.h has to link the same surface as test_graph_nav.
test_mod_voice_extra = files('../src/gui.c', '../src/gui_layer.c')

foreach name : test_names
  extra_srcs = []
  link_libs = [core_lib]
  if name == 'test_graph_nav'
    extra_srcs = test_graph_nav_extra
    link_libs = [core_lib, seq_lib, vizfx_lib]
  elif name == 'test_mod_voice'
    extra_srcs = test_mod_voice_extra
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