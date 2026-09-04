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
  files('main.c', 'viz.c', 'audio.c', 'rect.c'),
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

xvfb = find_program('xvfb-run', required: false)
if xvfb.found()
  t = executable('test_viz_glsl',
    files('test_viz_glsl.c', 'viz.c'),
    c_args: ['-DVIZ_INCLUDE_DIR="' + vizulobe_inc_dir + '"'],
    link_args: ['-rdynamic'],
    link_with: [vizulobe_core, core_lib],
    include_directories: inc,
    dependencies: [system_deps, cjson, cc.find_library('dl', required: false)],
  )
  test('vizulobe_test_viz_glsl', xvfb,
    args: ['-a', '-s', '-screen 0 640x480x24', t],
    timeout: 60,
  )

  smoke = executable('vizulobe_smoke',
    files('test_smoke.c', 'viz.c', 'audio.c', 'rect.c'),
    c_args: ['-DVIZ_INCLUDE_DIR="' + vizulobe_inc_dir + '"', '-DVIZ_HARNESS'],
    link_args: ['-rdynamic'],
    link_with: [vizulobe_core, core_lib],
    include_directories: inc,
    dependencies: [system_deps, cjson, cc.find_library('dl', required: false)],
  )
  test('vizulobe_smoke', xvfb,
    args: ['-a', '-s', '-screen 0 1280x800x24', smoke,
      '--script', meson.current_source_dir() / 'smoke_script.txt'],
    workdir: meson.project_source_root(),
    timeout: 60,
  )
endif
