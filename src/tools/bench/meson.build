# spectrax_bench — the shared benchmark suite.
#
# The bench is its own target: it is never linked into the app, and it is
# the only thing the deploy scripts push to a device besides the app.

bench_sources = files(
  'main.c',
  'bench_report.c',
  'bench_xvfb.c',
  'bench_audio.c',
  'bench_fileio.c',
  'bench_gfx.c',
  'bench_mod.c',
)

executable('spectrax_bench',
  bench_sources,
  link_with: [core_lib, seq_lib, vizfx_lib, gui_lib],
  c_args: ['-DSPECTRAX_BENCH'],
  include_directories: inc,
  dependencies: system_deps,
)
