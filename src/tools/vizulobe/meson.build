# vizulobe — audio-reactive visualisation sandbox.
# Build: ninja -C build vizulobe

cjson = dependency('libcjson')

vizulobe_core = static_library('vizulobe_core',
  files('scene.c', 'analysis.c', 'project.c'),
  include_directories: inc,
  dependencies: cjson,
)

vizulobe = executable('vizulobe',
  files('main.c'),
  link_with: [vizulobe_core],
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
