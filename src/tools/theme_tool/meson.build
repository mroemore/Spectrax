# theme_tool -- the companion tool for
# manual/tutorials/shipping-a-theme.md.
#
# It answers the config-resolution and theme-validation questions the
# tutorial asks, using the app's own config code (src/paths.c for the
# directory resolution, src/io/config_io.c for the theme load/save and
# the canonical path table) and the app's own default colour scheme
# (src/gui_core.c) so its answers cannot drift from the app's.
#
# Development tool: nothing here is linked into `spectrax`, and
# meson/install.sh does not copy it into bin/.

theme_tool = executable('theme_tool',
  files('theme_tool.c',
        '../../gui_core.c',
        '../../gui_arranger.c',
        '../../gui_pattern.c',
        '../../gui_instrument.c',
        '../../gui_inst_fm.c',
        '../../gui_inst_sample.c',
        '../../gui_inst_mod.c',
        '../../gui_inst_blep.c',
        '../../gui_mod_screen.c',
        '../../gui_layer.c',
        '../../curve_icons.c'),
  link_with: [core_lib, seq_lib, vizfx_lib],
  include_directories: inc,
  dependencies: system_deps,
)

test('theme_tool_selftest', theme_tool, args: ['--selftest'])
