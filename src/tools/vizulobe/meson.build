# vizulobe — audio-reactive visualisation sandbox.
# Build: ninja -C build vizulobe

vizulobe_inc_dir = meson.project_source_root() / 'include'

vizulobe = executable('vizulobe',
  files('main.c'),
  include_directories: inc,
  dependencies: system_deps,
)
