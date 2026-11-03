#ifndef SPECTRAX_AUDIO_DEVICE_H
#define SPECTRAX_AUDIO_DEVICE_H

#include "portaudio.h"

/* Opening the output stream.
 *
 * The rate and the buffer size are separate things and PortAudio takes
 * them in separate slots: Pa_OpenStream(stream, input, output,
 * sampleRate, framesPerBuffer, ...). Putting the buffer size in the
 * rate's slot is a mistake the compiler does not catch -- both are
 * numbers -- and the result is a stream that runs at 256 Hz while the
 * engine synthesises at 48000, which is silence or a note played 187
 * times too slow.
 *
 * So the opening lives here, in one place, with the two values named,
 * and a test checks it against a real device.
 */

/* Open one output stream.
 *
 * `rate` is the sample rate to ask for and `framesPerBuffer` the
 * callback's size. The device either honours both or the caller checks
 * what was negotiated: PortAudio substitutes silently, so the caller has
 * to look.
 *
 * Returns paNoError and fills `*stream` when the stream opened. */
PaError audioDeviceOpenStream(PaStream **stream, const PaStreamParameters *out,
                               double rate, unsigned long framesPerBuffer,
                               PaStreamCallback *callback, void *userData);

/* The sample rate the device actually negotiated. Zero when there is no
 * stream.
 *
 * PortAudio's stream info carries the rate but not the buffer size, so
 * the buffer is not reported here: a device may adjust it, and the
 * engine does not care -- the buffer size is a latency knob, not a
 * synthesis input. */
double audioDeviceNegotiatedRate(const PaStream *stream);

#endif
