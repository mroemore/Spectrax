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

foreach name : test_names
  extra_srcs = name == 'test_graph_nav' ? test_graph_nav_extra : files()
  exe = executable(name,
    files('dsp' / name + '.c'),
    extra_srcs,
    link_with: name == 'test_graph_nav' ? [core_lib, seq_lib, vizfx_lib] : core_lib,
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