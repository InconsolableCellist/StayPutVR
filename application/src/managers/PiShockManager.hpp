#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>

#include "../../../common/Config.hpp"
#include "../../../common/Logger.hpp"
#include "../../../common/HttpClient.hpp"

namespace StayPutVR {

    // Callback types for PiShock events
    using PiShockActionCallback = std::function<void(const std::string& action_type, bool success, const std::string& message)>;

    enum class PiShockActionType {
        BEEP = 2,
        VIBRATE = 1,
        SHOCK = 0
    };

    struct PiShockActionData {
        PiShockActionType type;
        int intensity;  // 1-100
        int duration;   // 1-15 seconds
        std::string reason;
    };

    class PiShockManager {
    public:
        PiShockManager();
        ~PiShockManager();

        // Initialization and cleanup
        bool Initialize(Config* config);
        void Shutdown();
        void Update(); // Called from main update loop

        // Configuration validation
        bool ValidateConfiguration() const;
        bool IsEnabled() const { return config_ && config_->pishock_enabled && config_->pishock_user_agreement; }
        bool IsFullyConfigured() const;
        
        // Action triggering
        void TriggerDisobedienceActions(const std::string& device_serial = "");
        void TriggerWarningActions(const std::string& device_serial = "");
        void TestActions();
        
        // Individual action methods
        void SendBeep(int intensity = 0, int duration = 1, const std::string& reason = "");
        void SendVibrate(int intensity, int duration, const std::string& reason = "");
        void SendShock(int intensity, int duration, const std::string& reason = "");
        
        // Utility functions
        std::string GetConnectionStatus() const;
        std::string GetLastError() const { return last_error_; }
        bool CanTriggerAction() const; // Rate limiting check
        
        // Event callbacks
        void SetActionCallback(PiShockActionCallback callback) { action_callback_ = callback; }
        
        // Configuration helpers
        static int ConvertIntensityToAPI(float normalized_intensity); // 0.0-1.0 -> 1-100
        static int ConvertDurationToAPI(float duration_seconds);      // 1.0-15.0 seconds -> 1-15 integer
        
    private:
        // Configuration
        Config* config_;
        
        // State tracking
        std::atomic<bool> enabled_;
        std::atomic<bool> user_agreement_;
        
        // Rate limiting
        mutable std::chrono::steady_clock::time_point last_action_time_;
        mutable std::mutex rate_limit_mutex_;
        static constexpr int RATE_LIMIT_SECONDS = 2;
        
        mutable std::chrono::steady_clock::time_point last_shock_time_;
        mutable std::mutex shock_cooldown_mutex_;
        
        // Error handling
        std::string last_error_;
        mutable std::mutex error_mutex_;
        
        // Event callback
        PiShockActionCallback action_callback_;
        
        // Internal methods
        void SetError(const std::string& error);
        bool CheckRateLimit();
        void UpdateRateLimit();
        bool CheckShockCooldown();
        void UpdateShockCooldown();
        
        // Action execution
        void ExecuteAction(const PiShockActionData& action);
        void ExecuteActionAsync(const PiShockActionData& action);
        
        // Validation helpers
        bool ValidateCredentials() const;
        bool ValidateActionParameters(int intensity, int duration) const;
        
        // Logging helpers
        void LogAction(const PiShockActionData& action, bool success, const std::string& response) const;
        std::string ActionTypeToString(PiShockActionType type) const;
    };

} // namespace StayPutVR 