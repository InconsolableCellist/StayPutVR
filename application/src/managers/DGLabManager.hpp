#pragma once

// DG-Lab Coyote 3.0 integration via the official app-bridged WebSocket
// protocol ("DGLAB-SOCKET"). We run an embedded WebSocket server; the user
// scans a QR code with the DG-Lab phone app, which then relays our commands
// to the Coyote over Bluetooth.
//
// Control model (mirrors Shocking-VRChat, the reference implementation):
// channel strength is pinned at the configured per-channel limit — strength
// alone produces no output — and punishments are delivered as pulse waveform
// messages whose per-25ms strength bytes scale with the requested intensity.
// A waveform of N entries plays N*100ms and then stops on its own, so no
// stop timer is needed. The phone app's own soft limits are reported back to
// us and always respected (effective limit = min(ours, theirs)).

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../../../common/Config.hpp"
#include "../network/DGLabServer.hpp"

namespace StayPutVR {

    using DGLabActionCallback = std::function<void(const std::string& action_type, bool success, const std::string& message)>;

    class DGLabManager {
    public:
        enum class LinkState {
            ServerStopped,
            WaitingForScan,   // server up, no phone bound
            Binding,          // phone connected, bind handshake in flight
            Bound             // ready to fire
        };

        DGLabManager() = default;
        ~DGLabManager();

        bool Initialize(Config* config);
        void Shutdown();
        void Update(); // main-loop pump: server lifecycle, events, heartbeat

        bool IsEnabled() const {
            return config_ && config_->dglab_enabled && config_->dglab_user_agreement;
        }
        bool IsServerRunning() const { return server_ && server_->IsRunning(); }
        bool IsBound() const { return link_state_ == LinkState::Bound; }
        LinkState GetLinkState() const { return link_state_; }

        std::string GetConnectionStatus() const;
        std::string GetLastError() const;
        // ws:// URL the QR code encodes; empty when the server is down.
        std::string GetQrPayload() const;

        // Live values echoed by the phone app (per channel index 0=A, 1=B).
        int GetStrength(int channel) const { return strength_[channel & 1]; }
        int GetAppLimit(int channel) const { return app_limit_[channel & 1]; }

        // Fire a pulse. intensity 0..1, duration in seconds (capped at 10s =
        // one pulse message). An empty device_serial fires all enabled
        // channels; a serial fires only the channels bound to it in the
        // Devices tab (used by bite-zone routing).
        void TriggerShock(float intensity, float duration_seconds, const std::string& reason = "",
                          const std::string& device_serial = "");
        // Zone-driven triggers. An empty serial means "all enabled channels";
        // a specific serial fires only the channels bound to it in the Devices
        // tab (no fall-back — a tracker bound to nothing fires nothing).
        void TriggerWarningActions(const std::string& device_serial = "");
        void TriggerDisobedienceActions(const std::string& device_serial = "");
        // Rate-limit probe for callers that poll while a device stays out of bounds.
        bool CanTriggerAction() const;
        // Single-channel variant used by the panel test buttons. channel 0=A, 1=B.
        void TestChannel(int channel, float intensity, float duration_seconds);
        // Immediately clear queued waveforms and zero both channels.
        void StopAll();

        void SetActionCallback(DGLabActionCallback callback) { action_callback_ = std::move(callback); }

        // Re-send the configured strength limits to the device (call after the
        // limit sliders change while bound).
        void ApplyStrengthLimits();

    private:
        void EnsureServerState();
        void HandleEvent(const DGLabServer::Event& event);
        void HandleAppMessage(const std::string& text);
        void SendJson(const std::string& type, const std::string& message);
        void SendStrengthSet(int channel, int value);
        void SendPulse(int channel, float intensity, float duration_seconds);
        void SetError(const std::string& error);
        int EffectiveLimit(int channel) const;
        bool ChannelEnabled(int channel) const;
        // Resolve which channels a trigger targets. Empty serial => every
        // globally-enabled channel; otherwise the Devices-tab binding for that
        // serial, intersected with the globally-enabled channels.
        std::array<bool, 2> ResolveChannels(const std::string& device_serial) const;
        // Shared body for the zone triggers: fires `channels` at intensity for
        // duration, subject to the rate limit.
        void FirePulse(const std::array<bool, 2>& channels, float intensity,
                       float duration_seconds, const std::string& reason);
        static std::string GenerateUuid();

        Config* config_ = nullptr;
        std::unique_ptr<DGLabServer> server_;

        std::atomic<LinkState> link_state_{LinkState::ServerStopped};
        // Id we assign to the phone app connection. Written on the main thread
        // during the bind handshake, but read by SendJson, which the OSC receive
        // thread reaches via the emergency-stop StopAll() -- so it needs a lock.
        std::string target_id_;
        mutable std::mutex target_id_mutex_;

        int strength_[2] = {0, 0};   // live strength echoed by the app
        int app_limit_[2] = {200, 200}; // soft limits configured in the phone app

        std::chrono::steady_clock::time_point last_heartbeat_;
        std::chrono::steady_clock::time_point last_pulse_;
        std::chrono::steady_clock::time_point last_server_attempt_;
        // Warning-zone triggers get their own timer so a stream of warnings
        // never consumes the disobedience budget (mirrors the other managers).
        std::chrono::steady_clock::time_point last_warning_;
        static constexpr int RATE_LIMIT_MS = 500;

        mutable std::mutex error_mutex_;
        std::string last_error_;

        DGLabActionCallback action_callback_;
    };

} // namespace StayPutVR
