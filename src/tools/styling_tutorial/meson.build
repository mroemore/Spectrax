# styling_tutorial -- the self-contained demo for
# manual/tutorials/building-a-component.md.
#
# Its own build target: nothing here is linked into `spectrax`, and the
# demo reads its own config (src/tools/styling_tutorial/layout.json and
# clr.json) instead of the shipped one. It links the style core only
# (gui_style.c + graph_gui.c) plus the curve-icon helper.
#
# The default paths are compiled in so the demo can be started from any
# working directory; --layout / --theme / --asset-dir override them.

styling_tutorial = executable('styling_tutorial',
  files('styling_tutorial.c', '../../curve_icons.c'),
  link_with: [core_lib],
  include_directories: inc,
  dependencies: system_deps,
  c_args: [
    '-DSPECTRAX_TUTORIAL_LAYOUT_JSON="' + meson.current_source_dir() + '/layout.json"',
    '-DSPECTRAX_TUTORIAL_THEME_JSON="' + meson.current_source_dir() + '/clr.json"',
    '-DSPECTRAX_TUTORIAL_ASSET_DIR="' + meson.project_source_root() + '/bin"',
  ],
)

# The headless half of the demo: resolved styles + reflowed geometry.
test('styling_tutorial_selftest', styling_tutorial, args: ['--selftest'])
