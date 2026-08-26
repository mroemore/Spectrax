# Tests: one executable per dsp/ test file, all linking the core object set.
# test_sequencer additionally links seq_lib (it stubs gui's rebuildPatternGraph,
# which appstate.o references). Registered with meson's test() runner.

test_names = [
  'test_io',
  'test_voice',
  'test_modsystem',
  'test_mod_voice',
  'test_graph_nav',
]

foreach name : test_names
  exe = executable(name,
    files('dsp' / name + '.c'),
    link_with: core_lib,
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