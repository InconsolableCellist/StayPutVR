#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>

#include "../../../common/Config.hpp"
#include "../../../common/Logger.hpp"
#include "../../../common/WebSocketClient.hpp"
#include "../../../common/HttpClient.hpp"
#include "../../../common/AsyncWorkQueue.hpp"
#include "../../../common/LinkStatus.hpp"
#include <nlohmann/json.hpp>

namespace StayPutVR {

    // Callback types for PiShock WebSocket events
    using PiShockWSActionCallback = std::function<void(const std::string& action_type, bool success, const std::string& message)>;

    enum class PiShockWSActionType {
        BEEP,      // 'b'
        VIBRATE,   // 'v'
        SHOCK,     // 's'
        STOP       // 'e'
    };

    struct PiShockWSActionData {
        PiShockWSActionType type;
        int intensity;  // 1-100
        int duration;   // milliseconds (converted from seconds)
        std::string reason;
    };

    class PiShockWebSocketManager {
    public:
        PiShockWebSocketManager();
        ~PiShockWebSocketManager();

        // Initialization and cleanup
        bool Initialize(Config* config);
        void Shutdown();
        void Update(); // Called from main update loop

        // Connection management
        bool Connect();
        void Disconnect();
        bool IsConnected() const;

        // Clear the reconnect backoff / give-up state and re-arm auto-reconnect
        // so Update() resumes attempts immediately. Wired to the Status tab
        // "Reconnect now" button.
        void RequestReconnect() { want_connected_ = true; connect_backoff_.Resume(); }
        bool ReconnectGaveUp() const { return connect_backoff_.GaveUp(); }
        
        // Configuration validation
        bool ValidateConfiguration() const;
        bool IsEnabled() const { return config_ && config_->pishock_enabled && config_->pishock_user_agreement; }
        bool IsFullyConfigured() const;
        
        // Action triggering
        void TriggerDisobedienceActions(const std::string& device_serial = "");
        void TriggerWarningActions(const std::string& device_serial = "");
        // Fire a direct shock at an explicit intensity (0..1) and duration
        // (seconds) on all configured shockers. Used by external triggers.
        void TriggerShock(float intensity, float duration_seconds, const std::string& reason = "");
        // Like TriggerShock but fires each shocker at its per-device disobedience
        // intensity instead of a single supplied intensity (for OSC bite/shock).
        void TriggerShockIndividual(float duration_seconds, const std::string& reason = "");
        void TestActions();
        
        // Individual action methods
        void SendBeep(int duration = 1, const std::string& reason = "");
        void SendVibrate(int intensity, int duration, const std::string& reason = "");
        void SendShock(int intensity, int duration, const std::string& reason = "");
        
        // Utility functions
        std::string GetConnectionStatus() const;
        std::string GetLastError() const { return last_error_; }
        bool CanTriggerAction() const; // Rate limiting check
        
        // Event callbacks
        void SetActionCallback(PiShockWSActionCallback callback) { action_callback_ = callback; }
        
        // Configuration helpers
        static int ConvertIntensityToAPI(float normalized_intensity); // 0.0-1.0 -> 1-100
        static int ConvertDurationToAPI(float duration_seconds);      // seconds -> milliseconds
        
    private:
        // Configuration
        Config* config_;
        
        // WebSocket client
        std::unique_ptr<WebSocketClient> ws_client_;
        
        // State tracking
        std::atomic<bool> enabled_;
        std::atomic<bool> user_agreement_;
        std::atomic<bool> connected_;

        // Auto-reconnect: want_connected_ records that the user asked to be
        // connected (set by Connect(), cleared by Disconnect()), so Update()
        // only retries a link that should be up. connect_backoff_ governs the
        // retry cadence; both are only touched from the main/UI thread.
        std::atomic<bool> want_connected_{false};
        ReconnectBackoff connect_backoff_;
        
        // Rate limiting
        mutable std::chrono::steady_clock::time_point last_action_time_;
        // Separate timer for warning-zone actions so a stream of warnings never
        // consumes the disobedience/shock budget (see CheckWarningRateLimit).
        mutable std::chrono::steady_clock::time_point last_warning_time_;
        mutable std::mutex rate_limit_mutex_;
        static constexpr int RATE_LIMIT_SECONDS = 2;
        
        mutable std::chrono::steady_clock::time_point last_shock_time_;
        mutable std::mutex shock_cooldown_mutex_;
        
        // Ping keepalive
        std::chrono::steady_clock::time_point last_ping_time_;
        static constexpr int PING_INTERVAL_SECONDS = 30;
        
        // Error handling
        std::string last_error_;
        mutable std::mutex error_mutex_;
        
        // Event callback
        PiShockWSActionCallback action_callback_;

        // Bounded async work queue (replaces detached threads)
        AsyncWorkQueue work_queue_;

        // WebSocket callbacks
        void OnWebSocketConnected();
        void OnWebSocketDisconnected(const std::string& reason);
        void OnWebSocketMessage(const std::string& message);
        void OnWebSocketError(const std::string& error);
        
        // Internal methods
        void SetError(const std::string& error);
        bool CheckRateLimit();
        bool CheckWarningRateLimit();
        void UpdateRateLimit();
        bool CheckShockCooldown();
        void UpdateShockCooldown();
        
        // Action execution
        void ExecuteAction(const PiShockWSActionData& action);
        void ExecuteActionAsync(const PiShockWSActionData& action);
        
        // Validation helpers
        bool ValidateCredentials() const;
        bool ValidateActionParameters(int intensity, int duration) const;
        bool FetchUserId();  // Fetch User ID from PiShock API
        
        // WebSocket protocol methods
        bool SendPing();
        bool SendPublishCommand(const std::string& mode, int intensity, int duration, const std::string& origin);
        bool SendPublishCommandMulti(const std::vector<int>& shocker_ids, const std::string& mode, int intensity, int duration, const std::string& origin);
        std::string GetChannelTarget() const;
        
        // Multi-device methods
        void SendBeepMulti(int duration, const std::string& reason, const std::string& device_serial);
        // intensity_override: API intensity (0..100) to use for every device;
        // when < 0 the configured disobedience intensities are used.
        void SendVibrateMulti(int duration, const std::string& reason, const std::string& device_serial, int intensity_override = -1);
        void SendShockMulti(int duration, const std::string& reason, const std::string& device_serial, int intensity_override = -1);
        
        // Logging helpers
        void LogAction(const PiShockWSActionData& action, bool success, const std::string& response) const;
        std::string ActionTypeToString(PiShockWSActionType type) const;
        std::string ActionTypeToMode(PiShockWSActionType type) const;
    };

} // namespace StayPutVR

