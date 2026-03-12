#include "system_audio_capturer.h"

#ifdef __linux__
#include "platform/linux/pulse_audio_capturer.h"
#elif defined(_WIN32)
#include "platform/windows/wasapi_loopback_capturer.h"
#endif

namespace flutter_webrtc_plugin {

std::unique_ptr<SystemAudioCapturer> SystemAudioCapturer::Create() {
#ifdef __linux__
  return std::make_unique<PulseAudioCapturer>();
#elif defined(_WIN32)
  return std::make_unique<WasapiLoopbackCapturer>();
#else
  return nullptr;
#endif
}

}  // namespace flutter_webrtc_plugin
