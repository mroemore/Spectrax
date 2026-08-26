# instrument_harness — boots straight to the FM instrument screen for manual
# and scripted (Xvfb) UI testing. Links the full app source set; main.c is
# compiled with -DSPECTRAX_HARNESS so its own main() is excluded (leaving
# initApplication + helpers). Run from bin/ (cwd-relative resources/data).

executable('instrument_harness',
  files('instrument_harness.c'),
  '../../main.c',
  '../../gui.c',
  '../../distortion.c',
  '../../dataviz.c',
  link_with: [core_lib, seq_lib,],
  c_args: ['-DSPECTRAX_HARNESS'],
  include_directories: inc,
  dependencies: system_deps,
)