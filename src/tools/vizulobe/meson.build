# vizulobe — audio-reactive visualisation sandbox.
# Build: ninja -C build vizulobe

cjson = dependency('libcjson')
vizulobe_inc_dir = meson.current_source_dir() / '..' / '..' / '..' / 'include'

vizulobe_core = static_library('vizulobe_core',
  files('scene.c', 'analysis.c', 'project.c'),
  include_directories: inc,
  dependencies: cjson,
)

vizulobe = executable('vizulobe',
  files('main.c', 'viz.c'),
  c_args: ['-DVIZ_INCLUDE_DIR="' + vizulobe_inc_dir + '"'],
  link_args: ['-rdynamic'],
  link_with: [vizulobe_core, core_lib],
  include_directories: inc,
  dependencies: [system_deps, cjson],
)

foreach tname : ['test_scene', 'test_analysis', 'test_project']
  t = executable(tname,
    files(tname + '.c'),
    link_with: [vizulobe_core, core_lib],
    include_directories: inc,
    dependencies: [system_deps, cjson],
  )
  test('vizulobe_' + tname, t)
endforeach

tc = executable('test_viz_c',
  files('test_viz_c.c', 'viz.c'),
  c_args: ['-DVIZ_INCLUDE_DIR="' + vizulobe_inc_dir + '"'],
  link_args: ['-rdynamic'],
  link_with: [vizulobe_core, core_lib],
  include_directories: inc,
  dependencies: system_deps,
)
test('vizulobe_test_viz_c', tc)
