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

# Coverage builds enable testcase() and disable assert()/ALWAYS()/NEVER()
# assertions (via NDEBUG). Both would otherwise compile to decision edges no
# test can take, i.e. uncoverable branches at every site. Run the suite under
# a debug build to enforce the contracts, under a coverage build to measure.
if get_option('b_coverage')
  add_project_arguments('-DSPX_COVERAGE_TEST=1', '-DNDEBUG', language: 'c')
endif

# Bundled third-party libraries live in include/ (headers) and lib/<arch>
# (static libs). raylib + kissfft are vendored; portaudio is a system dep.
# The target lib directory comes from the cross file (spectrax_libdir) and
# falls back to the host bundle for native builds. Native files may also
# carry the property, so a native build can be pointed at an arm bundle.
spectrax_libdir = meson.get_external_property('spectrax_libdir', 'lib/linux')
spectrax_incdir = meson.get_external_property('spectrax_incdir', 'include')
spectrax_platform = meson.get_external_property('spectrax_platform', 'desktop')
libdir = meson.current_source_dir() / spectrax_libdir

# Cross bundles ship link-time stubs for the libraries the device provides
# at run time (EGL, GLESv2, GBM, DRM, ALSA): right SONAME, right symbol
# names, no code. The loader resolves the symbols from the device's real
# libraries.
stubdir = meson.current_source_dir() / spectrax_libdir / 'stub'
stubdirs = []
if run_command('test', '-d', stubdir, check: false).returncode() == 0
  stubdirs = [stubdir]
endif

raylib = cc.find_library('raylib', dirs: libdir)
kissfft = cc.find_library('kissfft-float', dirs: libdir)
portaudio = cc.find_library('portaudio', dirs: libdir, required: false)
if not portaudio.found()
  portaudio = cc.find_library('portaudio')
endif

gl = cc.find_library('GL', required: false)
egl = declare_dependency()
glesv2 = declare_dependency()
gbm = declare_dependency()
drm = declare_dependency()
asound = declare_dependency()
if stubdirs.length() > 0
  egl = cc.find_library('EGL', dirs: stubdirs, required: false)
  glesv2 = cc.find_library('GLESv2', dirs: stubdirs, required: false)
  gbm = cc.find_library('gbm', dirs: stubdirs, required: false)
  drm = cc.find_library('drm', dirs: stubdirs, required: false)
  asound = cc.find_library('asound', dirs: stubdirs, required: false)
endif
x11 = cc.find_library('X11', required: false)
# The dev harnesses' X11 screenshot path is host-only: a DRM/KMS device
# build has no X server.
x11_cargs = x11.found() ? ['-DSPECTRAX_HAVE_X11'] : []
rt = cc.find_library('rt', required: false)
dl = cc.find_library('dl', required: false)
m = cc.find_library('m', required: false)

# Every target needs the vendored headers plus the project headers.
# The target headers (raylib.h, portaudio.h) live in include/<arch>; the
# project's own headers stay in include/. The target directory comes first
# so the host's raylib.h never shadows it.
inc = include_directories('include', 'src')
if spectrax_incdir != 'include'
  inc = include_directories(spectrax_incdir, 'include', 'src')
endif

cjson_inc = include_directories('third_party/cjson')
cjson_lib = static_library('spectrax_cjson', 'third_party/cjson/cJSON.c',
  include_directories: [inc, cjson_inc],
)
cjson_dep = declare_dependency(link_with: cjson_lib, include_directories: cjson_inc)

system_deps = [raylib, kissfft, portaudio, egl, glesv2, gbm, drm, asound,
                gl, x11, rt, dl, m, cjson_dep]

# Install step copies the freshly built binaries into bin/ so the app can
# be launched with cwd=bin/ and pick up the cwd-relative resources/ and
# data/ paths (see run-from-bin convention). A cross build must not do
# that: bin/ holds the host build, and a device deploy takes its binaries
# straight from the build directory.
meson.add_install_script('meson/install.sh', spectrax_libdir)

subdir('src')
subdir('tests')
subdir('src/tools/nav_harness')
subdir('src/tools/instrument_harness')
subdir('src/tools/sample_analyser')
subdir('src/tools/styling_tutorial')
subdir('src/tools/theme_tool')
subdir('src/tools/vizulobe')
subdir('src/tools/bench')
