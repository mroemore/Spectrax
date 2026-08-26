# sample_analyser — standalone WAV inspector (inspectro_wavget).
# Build: ninja -C build inspectro_wavget

executable('inspectro_wavget',
  files('main.c', 'sa_gui.c', 'sa_audio.c'),
  '../../input.c',
  '../../io.c',
  '../../sample.c',
  '../../fft.c',
  '../../dataviz.c',
  include_directories: inc,
  dependencies: [raylib, kissfft, portaudio, gl, x11, rt, dl, m],
)