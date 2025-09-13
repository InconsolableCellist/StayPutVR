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

    // Callback types for OpenShock events
    using OpenShockActionCallback = std::function<void(const std::string& action_type, bool success, const std::string& message)>;

    enum class OpenShockActionType {
        SHOCK = 0,
        VIBRATE = 1,
        SOUND = 2  // OpenShock uses "sound" instead of "beep"
    };

    struct OpenShockActionData {
        OpenShockActionType type;
        int intensity;  // 1-100
        int duration;   // Duration in milliseconds (OpenShock uses ms, not seconds)
        std::string reason;
    };

    class OpenShockManager {
    public:
        OpenShockManager();
        ~OpenShockManager();

        // Initialization and cleanup
        bool Initialize(Config* config);
        void Shutdown();
        void Update(); // Called from main update loop

        // Configuration validation
        bool ValidateConfiguration() const;
        bool IsEnabled() const { return config_ && config_->openshock_enabled && config_->openshock_user_agreement; }
        bool IsFullyConfigured() const;
        
        // Action triggering
        void TriggerDisobedienceActions(const std::string& device_serial = "");
        void TriggerWarningActions(const std::string& device_serial = "");
        void TestActions();
        
        // Individual action methods
        void SendSound(int intensity = 0, int duration = 1000, const std::string& reason = "", const std::string& device_serial = "");
        void SendVibrate(int intensity, int duration, const std::string& reason = "", const std::string& device_serial = "");
        void SendShock(int intensity, int duration, const std::string& reason = "", const std::string& device_serial = "");
        
        // Utility functions
        std::string GetConnectionStatus() const;
        std::string GetLastError() const { return last_error_; }
        bool CanTriggerAction() const; // Rate limiting check
        
        // Event callbacks
        void SetActionCallback(OpenShockActionCallback callback) { action_callback_ = callback; }
        
        // Configuration helpers
        static int ConvertIntensityToAPI(float normalized_intensity); // 0.0-1.0 -> 1-100
        static int ConvertDurationToAPI(float normalized_duration);   // 0.0-1.0 -> 1000-15000 (ms)
        
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
        
        // Error handling
        std::string last_error_;
        mutable std::mutex error_mutex_;
        
        // Event callback
        OpenShockActionCallback action_callback_;
        
        // Internal methods
        void SetError(const std::string& error);
        bool CheckRateLimit();
        void UpdateRateLimit();
        
        // Action execution
        void ExecuteAction(const OpenShockActionData& action);
        void ExecuteActionAsync(const OpenShockActionData& action);
        void ExecuteActionAsyncMulti(const OpenShockActionData& action, const std::string& device_serial);
        void ExecuteActionMulti(const OpenShockActionData& action, const std::string& device_serial);
        
        // Validation helpers
        bool ValidateCredentials() const;
        bool ValidateActionParameters(int intensity, int duration) const;
        
        // Logging helpers
        void LogAction(const OpenShockActionData& action, bool success, const std::string& response) const;
        std::string ActionTypeToString(OpenShockActionType type) const;
    };

} // namespace StayPutVR
