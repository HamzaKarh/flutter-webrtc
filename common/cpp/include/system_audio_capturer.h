#ifndef SYSTEM_AUDIO_CAPTURER_H
#define SYSTEM_AUDIO_CAPTURER_H

#include <cstddef>
#include <cstdint>
#include <memory>

namespace flutter_webrtc_plugin {

// Platform-agnostic interface for capturing system/desktop audio.
// Implementations: PulseAudioCapturer (Linux), WasapiLoopbackCapturer (Windows).
class SystemAudioCapturer {
 public:
  virtual ~SystemAudioCapturer() = default;

  // Start capturing. Returns false on failure.
  virtual bool Start() = 0;

  // Stop capturing and release resources.
  virtual void Stop() = 0;

  // Read exactly one buffer of audio frames (blocking).
  // Returns the number of frames read, or 0 on error/stop.
  virtual size_t ReadFrames(int16_t* buffer, size_t buffer_frames) = 0;

  virtual int sample_rate() const = 0;
  virtual int channels() const = 0;
  virtual int bits_per_sample() const = 0;

  // Factory: returns the platform-appropriate implementation, or nullptr.
  static std::unique_ptr<SystemAudioCapturer> Create();
};

}  // namespace flutter_webrtc_plugin

#endif  // SYSTEM_AUDIO_CAPTURER_H
