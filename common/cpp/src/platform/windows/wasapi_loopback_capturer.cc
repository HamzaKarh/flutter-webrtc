#include "wasapi_loopback_capturer.h"

#include <functiondiscoverykeys_devpkey.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <algorithm>

// WASAPI loopback captures the system audio mix from the default render
// endpoint.  The mix format is typically 32-bit float, stereo, at the
// system sample rate (often 48 kHz).  We convert to 16-bit PCM mono at
// 48 kHz so the output matches the PulseAudio backend on Linux.

namespace flutter_webrtc_plugin {

static inline int16_t FloatToS16(float v) {
  v = std::clamp(v, -1.0f, 1.0f);
  return static_cast<int16_t>(v * 32767.0f);
}

WasapiLoopbackCapturer::WasapiLoopbackCapturer() = default;

WasapiLoopbackCapturer::~WasapiLoopbackCapturer() {
  Stop();
}

bool WasapiLoopbackCapturer::Start() {
  if (started_) return true;

  HRESULT hr;

  // COM must be initialized on the thread that calls WASAPI.
  // The capture thread in flutter_screen_capture.cc calls Start() from the
  // main thread, but ReadFrames() is called from the audio thread.
  // We initialize COM here; the audio thread must also initialize COM before
  // calling ReadFrames() — however, the simplest approach is to initialize
  // here for the setup and let the caller ensure the audio thread also has COM.
  hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (SUCCEEDED(hr) || hr == S_FALSE /* already initialized */) {
    com_initialized_ = true;
  } else if (hr == RPC_E_CHANGED_MODE) {
    // COM was already initialized with a different threading model.
    // This is acceptable — we can still use WASAPI.
    com_initialized_ = false;
  } else {
    std::cerr << "WASAPI: CoInitializeEx failed: 0x" << std::hex << hr
              << std::endl;
    return false;
  }

  // Get the default audio render endpoint.
  IMMDeviceEnumerator* enumerator = nullptr;
  hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                        __uuidof(IMMDeviceEnumerator),
                        reinterpret_cast<void**>(&enumerator));
  if (FAILED(hr)) {
    std::cerr << "WASAPI: Failed to create device enumerator: 0x" << std::hex
              << hr << std::endl;
    return false;
  }

  IMMDevice* device = nullptr;
  hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
  enumerator->Release();
  if (FAILED(hr)) {
    std::cerr << "WASAPI: Failed to get default render endpoint: 0x"
              << std::hex << hr << std::endl;
    return false;
  }

  hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                         reinterpret_cast<void**>(&audio_client_));
  device->Release();
  if (FAILED(hr)) {
    std::cerr << "WASAPI: Failed to activate audio client: 0x" << std::hex
              << hr << std::endl;
    return false;
  }

  // Get the mix format of the render endpoint.
  hr = audio_client_->GetMixFormat(&mix_format_);
  if (FAILED(hr)) {
    std::cerr << "WASAPI: Failed to get mix format: 0x" << std::hex << hr
              << std::endl;
    Stop();
    return false;
  }

  // Create an event for buffer-ready notifications.
  audio_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  if (!audio_event_) {
    std::cerr << "WASAPI: Failed to create event" << std::endl;
    Stop();
    return false;
  }

  // Initialize the audio client in loopback mode (shared).
  // Use a 20ms buffer — gives us comfortable room for 10ms reads.
  REFERENCE_TIME buffer_duration = 200000;  // 20ms in 100ns units
  hr = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                  AUDCLNT_STREAMFLAGS_LOOPBACK |
                                      AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                  buffer_duration, 0, mix_format_, nullptr);
  if (FAILED(hr)) {
    std::cerr << "WASAPI: Failed to initialize audio client: 0x" << std::hex
              << hr << std::endl;
    Stop();
    return false;
  }

  hr = audio_client_->SetEventHandle(audio_event_);
  if (FAILED(hr)) {
    std::cerr << "WASAPI: Failed to set event handle: 0x" << std::hex << hr
              << std::endl;
    Stop();
    return false;
  }

  hr = audio_client_->GetService(__uuidof(IAudioCaptureClient),
                                  reinterpret_cast<void**>(&capture_client_));
  if (FAILED(hr)) {
    std::cerr << "WASAPI: Failed to get capture client: 0x" << std::hex << hr
              << std::endl;
    Stop();
    return false;
  }

  hr = audio_client_->Start();
  if (FAILED(hr)) {
    std::cerr << "WASAPI: Failed to start capture: 0x" << std::hex << hr
              << std::endl;
    Stop();
    return false;
  }

  started_ = true;
  return true;
}

void WasapiLoopbackCapturer::Stop() {
  if (audio_client_ && started_) {
    audio_client_->Stop();
  }
  started_ = false;

  if (capture_client_) {
    capture_client_->Release();
    capture_client_ = nullptr;
  }
  if (audio_client_) {
    audio_client_->Release();
    audio_client_ = nullptr;
  }
  if (audio_event_) {
    CloseHandle(audio_event_);
    audio_event_ = nullptr;
  }
  if (mix_format_) {
    CoTaskMemFree(mix_format_);
    mix_format_ = nullptr;
  }
  if (com_initialized_) {
    CoUninitialize();
    com_initialized_ = false;
  }
}

void WasapiLoopbackCapturer::ConvertFrames(const BYTE* src,
                                            UINT32 frame_count,
                                            int16_t* dst,
                                            size_t dst_frames,
                                            size_t* frames_written) {
  if (!mix_format_ || frame_count == 0) {
    *frames_written = 0;
    return;
  }

  size_t src_channels = mix_format_->nChannels;
  bool is_float = false;

  // Check if the format is float (WAVEFORMATEXTENSIBLE or plain float).
  if (mix_format_->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
    is_float = true;
  } else if (mix_format_->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
    auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix_format_);
    if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
      is_float = true;
    }
  }

  size_t to_write = std::min(static_cast<size_t>(frame_count), dst_frames);

  if (is_float && mix_format_->wBitsPerSample == 32) {
    const float* fsrc = reinterpret_cast<const float*>(src);
    for (size_t i = 0; i < to_write; i++) {
      // Downmix to mono by averaging all channels.
      float sum = 0.0f;
      for (size_t ch = 0; ch < src_channels; ch++) {
        sum += fsrc[i * src_channels + ch];
      }
      dst[i] = FloatToS16(sum / static_cast<float>(src_channels));
    }
  } else if (!is_float && mix_format_->wBitsPerSample == 16) {
    const int16_t* ssrc = reinterpret_cast<const int16_t*>(src);
    for (size_t i = 0; i < to_write; i++) {
      int32_t sum = 0;
      for (size_t ch = 0; ch < src_channels; ch++) {
        sum += ssrc[i * src_channels + ch];
      }
      dst[i] = static_cast<int16_t>(sum / static_cast<int32_t>(src_channels));
    }
  } else {
    // Unsupported format — output silence.
    std::memset(dst, 0, to_write * sizeof(int16_t));
  }

  *frames_written = to_write;
}

size_t WasapiLoopbackCapturer::ReadFrames(int16_t* buffer,
                                           size_t buffer_frames) {
  if (!started_ || !capture_client_) return 0;

  size_t total_written = 0;

  while (total_written < buffer_frames) {
    // Wait for data with a 20ms timeout.
    // If no system audio is playing, WASAPI loopback produces no packets,
    // so we inject silence after the timeout to maintain cadence.
    DWORD wait_result = WaitForSingleObject(audio_event_, 20);

    if (!started_) return 0;  // Stopped while waiting.

    UINT32 packet_length = 0;
    HRESULT hr = capture_client_->GetNextPacketSize(&packet_length);
    if (FAILED(hr)) return 0;

    if (packet_length == 0) {
      // No data available (silence). Fill remainder with zeros.
      size_t remaining = buffer_frames - total_written;
      std::memset(buffer + total_written, 0, remaining * sizeof(int16_t));
      total_written = buffer_frames;
      break;
    }

    while (packet_length > 0) {
      BYTE* data = nullptr;
      UINT32 frames_available = 0;
      DWORD flags = 0;

      hr = capture_client_->GetBuffer(&data, &frames_available, &flags,
                                       nullptr, nullptr);
      if (FAILED(hr)) return total_written > 0 ? total_written : 0;

      if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
        // Silent buffer — fill with zeros.
        size_t to_fill =
            std::min(static_cast<size_t>(frames_available),
                     buffer_frames - total_written);
        std::memset(buffer + total_written, 0, to_fill * sizeof(int16_t));
        total_written += to_fill;
      } else {
        size_t frames_written = 0;
        ConvertFrames(data, frames_available, buffer + total_written,
                      buffer_frames - total_written, &frames_written);
        total_written += frames_written;
      }

      capture_client_->ReleaseBuffer(frames_available);

      if (total_written >= buffer_frames) break;

      hr = capture_client_->GetNextPacketSize(&packet_length);
      if (FAILED(hr)) break;
    }
  }

  return total_written;
}

}  // namespace flutter_webrtc_plugin
