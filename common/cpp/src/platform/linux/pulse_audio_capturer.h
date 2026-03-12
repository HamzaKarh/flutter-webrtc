#ifndef PULSE_AUDIO_CAPTURER_H
#define PULSE_AUDIO_CAPTURER_H

#include "system_audio_capturer.h"

#include <pulse/simple.h>

namespace flutter_webrtc_plugin {

class PulseAudioCapturer : public SystemAudioCapturer {
 public:
  PulseAudioCapturer();
  ~PulseAudioCapturer() override;

  bool Start() override;
  void Stop() override;
  size_t ReadFrames(int16_t* buffer, size_t buffer_frames) override;

  int sample_rate() const override { return 48000; }
  int channels() const override { return 1; }
  int bits_per_sample() const override { return 16; }

 private:
  pa_simple* pa_ = nullptr;
};

}  // namespace flutter_webrtc_plugin

#endif  // PULSE_AUDIO_CAPTURER_H
