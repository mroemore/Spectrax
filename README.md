## Ubuntu build instructions

Install gcc and make

``sudo apt install gcc make``

Install raylib dependencies:

``sudo apt install libasound2-dev libx11-dev libxrandr-dev libxi-dev libgl1-mesa-dev libglu1-mesa-dev libxcursor-dev libxinerama-dev libwayland-dev libxkbcommon-dev``

Install PortAudio

``sudo apt install libportaudio2 portaudio19-dev``

Clone the repo:

``git clone https://github.com/mroemore/Spectrax``

Build the application

``cd Spectrax``

``make``

Run the application

``cd bin``

``./spectrax``

## vizulobe

Audio-reactive visualisation sandbox. Captures system audio (Point it at your
'Monitor of ...' device) and runs a scene of GLSL `.frag` shaders and C snippets
compiled at runtime with TCC.

- `cd bin && ./vizulobe -p sample_project.json` (the sample project and its
  example vizzes are copied into `bin/` by the install step; sources live in
  `src/tools/vizulobe/`)
- `./vizulobe --list-devices` then `./vizulobe -d <device-or-index>`
- `L` load a fg viz, `B` load the bg viz, drag to move, ctrl+drag to resize,
  `S` save project, `R` load project, `Del` remove selected.
- C snippets: `#include "vizulobe.h"`, define `viz_frame(viz_t *ctx)`. Full
  raylib draw API available. Needs `tcc` on PATH (`sudo xbps-install tcc`).
- GLSL: see `src/tools/vizulobe/smoke_viz.frag` for the uniform set.

## Windows build instructions

Download and install Msys2:

https://www.msys2.org/

Install raylib dependencies:
TBC
