#ifndef WASAPI_LOOPBACK_CAPTURER_H
#define WASAPI_LOOPBACK_CAPTURER_H

#include "system_audio_capturer.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

namespace flutter_webrtc_plugin {

class WasapiLoopbackCapturer : public SystemAudioCapturer {
 public:
  WasapiLoopbackCapturer();
  ~WasapiLoopbackCapturer() override;

  bool Start() override;
  void Stop() override;
  size_t ReadFrames(int16_t* buffer, size_t buffer_frames) override;

  int sample_rate() const override { return 48000; }
  int channels() const override { return 1; }
  int bits_per_sample() const override { return 16; }

 private:
  // Convert captured WASAPI data to our output format (int16 mono 48kHz).
  void ConvertFrames(const BYTE* src, UINT32 frame_count,
                     int16_t* dst, size_t dst_frames,
                     size_t* frames_written);

  IAudioClient* audio_client_ = nullptr;
  IAudioCaptureClient* capture_client_ = nullptr;
  HANDLE audio_event_ = nullptr;
  WAVEFORMATEX* mix_format_ = nullptr;
  bool com_initialized_ = false;
  bool started_ = false;
};

}  // namespace flutter_webrtc_plugin

#endif  // WASAPI_LOOPBACK_CAPTURER_H
