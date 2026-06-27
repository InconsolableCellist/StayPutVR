#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

// Forward-declare the WASAPI COM interfaces so this header pulls in no Windows
// headers (keeps the Linux dev build clean). The real definitions are included
// only in the .cpp.
#ifdef _WIN32
struct IAudioClient;
struct IAudioCaptureClient;
#endif

namespace StayPutVR {

    struct MicAudioDevice {
        std::string id;    // stable WASAPI endpoint id (UTF-8); "" => system default
        std::string name;  // friendly name for the UI
    };

    // Captures the OS microphone via WASAPI shared-mode capture on a background
    // thread and publishes a smoothed RMS level in [0,1] for the enforced-mute
    // constraint and the VU meter. Modeled on YipCompanion's AudioCapture_windows:
    // stable-id selection with fallback-to-default, a mix-format negotiation
    // cascade, and in-loop reconnect on device invalidation. Windows-only; a
    // no-op stub keeps the Linux dev build compiling (GetLevel() returns 0).
    class MicrophoneManager {
    public:
        MicrophoneManager() = default;
        ~MicrophoneManager();

        bool Start();                 // spawn capture thread (retries init in-loop)
        void Stop();                  // signal + join; releases device on the thread
        bool IsRunning() const { return running_.load(); }
        bool IsConnected() const { return connected_.load(); }

        // Smoothed RMS level in [0,1]. Written by the capture thread, read by the
        // UI/constraint thread. Lock-free. Used by the enforcement constraint.
        float GetLevel() const { return level_.load(std::memory_order_relaxed); }

        // Decaying peak-hold (fast attack, slow release) in [0,1], for the VU meter:
        // jumps to each new high then eases back down. Display-only.
        float GetPeak() const { return peak_.load(std::memory_order_relaxed); }

        // Select a capture device by stable id ("" = system default). Restarts
        // capture if running (Stop -> set -> Start), so the swap is atomic to callers.
        void SetDevice(const std::string& device_id);
        std::string GetCurrentDeviceId() const;
        std::string GetCurrentDeviceName() const;

        // Enumerate ACTIVE capture (input) endpoints. Safe on the UI thread (does
        // its own scoped COM init). Returns empty on non-Windows.
        std::vector<MicAudioDevice> GetDevices();

        std::string GetLastError() const;

    private:
#ifdef _WIN32
        bool InitDevice();            // capture-thread only
        void ReleaseDevice();         // capture-thread only
        void CaptureLoop();
        void SetError(const std::string& msg);
        float FrameToMono(const unsigned char* data, unsigned int frame) const;

        std::thread capture_thread_;

        IAudioClient* audio_client_ = nullptr;
        IAudioCaptureClient* capture_client_ = nullptr;
        int  source_sample_rate_ = 48000;
        int  source_channels_ = 1;
        bool source_is_float_ = true;
        int  source_bits_ = 32;
#endif
        std::atomic<bool> running_{false};
        std::atomic<bool> connected_{false};
        std::atomic<float> level_{0.0f};
        std::atomic<float> peak_{0.0f};

        mutable std::mutex meta_mutex_;          // guards the strings below
        std::string selected_device_id_;         // only mutated while stopped
        std::string current_device_name_ = "None";
        std::string last_error_;
    };

} // namespace StayPutVR
