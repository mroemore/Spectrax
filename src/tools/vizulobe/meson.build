# vizulobe — audio-reactive visualisation sandbox.
# Build: ninja -C build vizulobe

vizulobe_core = static_library('vizulobe_core',
  files('scene.c', 'analysis.c'),
  include_directories: inc,
)

vizulobe = executable('vizulobe',
  files('main.c'),
  link_with: [vizulobe_core],
  include_directories: inc,
  dependencies: system_deps,
)

foreach tname : ['test_scene', 'test_analysis']
  t = executable(tname,
    files(tname + '.c'),
    link_with: [vizulobe_core, core_lib],
    include_directories: inc,
    dependencies: system_deps,
  )
  test('vizulobe_' + tname, t)
endforeach
