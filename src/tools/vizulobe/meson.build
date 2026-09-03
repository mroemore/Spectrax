# vizulobe — audio-reactive visualisation sandbox.
# Build: ninja -C build vizulobe

vizulobe_inc_dir = meson.project_source_root() / 'include'

scene_lib = static_library('vizulobe_scene',
  files('scene.c'),
  include_directories: inc,
)

vizulobe = executable('vizulobe',
  files('main.c'),
  link_with: scene_lib,
  include_directories: inc,
  dependencies: system_deps,
)

t = executable('test_scene',
  files('test_scene.c'),
  link_with: scene_lib,
  include_directories: inc,
  dependencies: system_deps,
)
test('vizulobe_test_scene', t)