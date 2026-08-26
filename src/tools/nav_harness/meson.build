# nav_harness — visual testing harness for the graph navigation code.
# Build: ninja -C build nav_harness, then run from bin/ under Xvfb:
#   xvfb-run -a -s "-screen 0 1280x800x24" bin/nav_harness --out out

executable('nav_harness',
  files('nav_harness.c', '../../graph_gui.c', '../../dstruct.c'),
  c_args: ['-DDEBUG_GRAPH_DRAW'],
  include_directories: inc,
  dependencies: [raylib, kissfft, gl, x11, rt, dl, m],
)