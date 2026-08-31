project('spectrax', 'c',
  version: '0.2.0',
  license: 'MIT',
  default_options: [
    'c_std=gnu99',
    'warning_level=1',
    'buildtype=debug',
  ],
  meson_version: '>=0.60.0',
)

cc = meson.get_compiler('c')

# Bundled third-party libraries live in include/ (headers) and lib/linux
# (static libs). raylib + kissfft are vendored; portaudio is a system dep.
libdir = meson.current_source_dir() / 'lib' / 'linux'

raylib = cc.find_library('raylib', dirs: libdir)
kissfft = cc.find_library('kissfft-float', dirs: libdir)
portaudio = cc.find_library('portaudio')

gl = cc.find_library('GL', required: false)
x11 = cc.find_library('X11', required: false)
rt = cc.find_library('rt', required: false)
dl = cc.find_library('dl', required: false)
m = cc.find_library('m', required: false)

# Every target needs the vendored headers plus the project headers.
inc = include_directories('include', 'src')

cjson_inc = include_directories('third_party/cjson')
cjson_lib = static_library('spectrax_cjson', 'third_party/cjson/cJSON.c',
  include_directories: [inc, cjson_inc],
)
cjson_dep = declare_dependency(link_with: cjson_lib, include_directories: cjson_inc)

system_deps = [raylib, kissfft, portaudio, gl, x11, rt, dl, m, cjson_dep]

# Install step copies the freshly built binaries into bin/ so the app can
# be launched with cwd=bin/ and pick up the cwd-relative resources/ and
# data/ paths (see run-from-bin convention).
meson.add_install_script('meson/install.sh')

subdir('src')
subdir('tests')
subdir('src/tools/nav_harness')
subdir('src/tools/instrument_harness')
subdir('src/tools/sample_analyser')