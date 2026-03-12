#include "pulse_audio_capturer.h"

#include <pulse/error.h>
#include <iostream>

namespace flutter_webrtc_plugin {

PulseAudioCapturer::PulseAudioCapturer() = default;

PulseAudioCapturer::~PulseAudioCapturer() {
  Stop();
}

bool PulseAudioCapturer::Start() {
  if (pa_) return true;  // Already started.

  pa_sample_spec spec;
  spec.format = PA_SAMPLE_S16LE;
  spec.rate = sample_rate();
  spec.channels = channels();

  int pa_error;
  pa_ = pa_simple_new(
      nullptr,              // default server
      "havok-screenshare",  // app name
      PA_STREAM_RECORD,
      "@DEFAULT_MONITOR@",  // capture desktop audio output
      "screen-audio",       // stream description
      &spec, nullptr, nullptr, &pa_error);

  if (!pa_) {
    std::cerr << "PulseAudio monitor open failed: "
              << pa_strerror(pa_error) << std::endl;
    return false;
  }
  return true;
}

void PulseAudioCapturer::Stop() {
  if (pa_) {
    pa_simple_free(pa_);
    pa_ = nullptr;
  }
}

size_t PulseAudioCapturer::ReadFrames(int16_t* buffer, size_t buffer_frames) {
  if (!pa_) return 0;

  size_t buffer_bytes = buffer_frames * channels() * (bits_per_sample() / 8);
  int pa_error;
  if (pa_simple_read(pa_, buffer, buffer_bytes, &pa_error) < 0) {
    std::cerr << "PulseAudio read failed: "
              << pa_strerror(pa_error) << std::endl;
    return 0;
  }
  return buffer_frames;
}

}  // namespace flutter_webrtc_plugin
