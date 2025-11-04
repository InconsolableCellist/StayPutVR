#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <map>

#include "../../../common/Config.hpp"
#include "../../../common/Logger.hpp"
#include "../../../common/WebSocketClient.hpp"
#include <nlohmann/json.hpp>

namespace StayPutVR {

    // Callback types for Buttplug events
    using ButtplugActionCallback = std::function<void(const std::string& action_type, bool success, const std::string& message)>;

    enum class ButtplugZoneType {
        SAFE,
        WARNING,
        DISOBEDIENCE
    };

    struct ButtplugDeviceInfo {
        int device_index;
        std::string device_name;
        bool supports_vibration;
        int vibration_feature_count;
    };

    class ButtplugManager {
    public:
        ButtplugManager();
        ~ButtplugManager();

        // Initialization and cleanup
        bool Initialize(Config* config);
        void Shutdown();
        void Update(); // Called from main update loop

        // Connection management
        bool Connect();
        void Disconnect();
        bool IsConnected() const;
        
        // Configuration validation
        bool ValidateConfiguration() const;
        bool IsEnabled() const { return config_ && config_->buttplug_enabled && config_->buttplug_user_agreement; }
        bool IsFullyConfigured() const;
        
        // Action triggering
        void TriggerSafeZoneActions(const std::string& device_serial = "");
        void TriggerWarningActions(const std::string& device_serial = "");
        void TriggerDisobedienceActions(const std::string& device_serial = "");
        void ClearZoneState(const std::string& device_serial = ""); // Stop vibration when returning to safe/neutral
        void TestActions();
        void StopAllDevices();
        
        // Individual action methods
        void SendVibrate(int device_index, float intensity, float duration, const std::string& reason = "");
        void SendVibrateMulti(const std::vector<int>& device_indices, float intensity, float duration, const std::string& reason = "");
        void SendVibrateContinuous(int device_index, float intensity, const std::string& reason = "");
        void StopVibration(int device_index);
        void StopVibrationMulti(const std::vector<int>& device_indices);
        
        // Device enumeration
        bool RequestDeviceList();
        std::vector<ButtplugDeviceInfo> GetAvailableDevices() const;
        
        // Utility functions
        std::string GetConnectionStatus() const;
        std::string GetLastError() const { return last_error_; }
        bool CanTriggerAction() const; // Rate limiting check
        
        // Event callbacks
        void SetActionCallback(ButtplugActionCallback callback) { action_callback_ = callback; }
        
        // Configuration helpers
        static float ConvertIntensityToAPI(float normalized_intensity); // 0.0-1.0 -> 0.0-1.0 (passthrough for Buttplug)
        static int ConvertDurationToMilliseconds(float duration_seconds); // seconds -> milliseconds
        
    private:
        // Configuration
        Config* config_;
        
        // WebSocket client
        std::unique_ptr<WebSocketClient> ws_client_;
        
        // State tracking
        std::atomic<bool> enabled_;
        std::atomic<bool> user_agreement_;
        std::atomic<bool> connected_;
        std::atomic<bool> server_ready_;
        
        // Message ID tracking
        std::atomic<uint32_t> next_message_id_;
        
        // Device tracking
        mutable std::mutex devices_mutex_;
        std::map<int, ButtplugDeviceInfo> available_devices_; // device_index -> device info
        
        // Zone state tracking for continuous vibration
        mutable std::mutex zone_state_mutex_;
        std::map<std::string, ButtplugZoneType> current_zone_state_; // device_serial -> current zone
        
        // Rate limiting
        mutable std::chrono::steady_clock::time_point last_action_time_;
        mutable std::mutex rate_limit_mutex_;
        static constexpr int RATE_LIMIT_MILLISECONDS = 100; // 100ms minimum between actions
        
        // Ping keepalive (not required by Buttplug spec but can be used)
        std::chrono::steady_clock::time_point last_ping_time_;
        // TODO: use MaxPingTime (uint, ms) from ServerInfo response
        static constexpr int PING_INTERVAL_SECONDS = 30;
        
        // Error handling
        std::string last_error_;
        mutable std::mutex error_mutex_;
        
        // Event callback
        ButtplugActionCallback action_callback_;
        
        // WebSocket callbacks
        void OnWebSocketConnected();
        void OnWebSocketDisconnected(const std::string& reason);
        void OnWebSocketMessage(const std::string& message);
        void OnWebSocketError(const std::string& error);
        
        // Internal methods
        void SetError(const std::string& error);
        bool CheckRateLimit();
        void UpdateRateLimit();
        
        // Action execution
        void ExecuteZoneAction(ButtplugZoneType zone_type, const std::string& device_serial);
        
        // Validation helpers
        bool ValidateConnectionParameters() const;
        
        // Buttplug protocol methods
        bool SendRequestServerInfo();
        bool SendStartScanning();
        bool SendStopScanning();
        bool SendScalarCmd(int device_index, float scalar, const std::string& actuator_type = "Vibrate");
        bool SendStopDeviceCmd(int device_index);
        bool SendStopAllDevices();
        bool SendPing();
        
        // Message handling
        void HandleServerInfo(const nlohmann::json& message);
        void HandleDeviceAdded(const nlohmann::json& message);
        void HandleDeviceRemoved(const nlohmann::json& message);
        void HandleDeviceList(const nlohmann::json& message);
        void HandleOk(const nlohmann::json& message);
        void HandleError(const nlohmann::json& message);
        
        // Helper methods
        uint32_t GetNextMessageId();
        void AddDevice(const nlohmann::json& device_json);
        void RemoveDevice(int device_index);
        
        // Multi-device methods
        void VibrateConfiguredDevices(float intensity, float duration, const std::string& reason);
        std::vector<int> GetEnabledDeviceIndices(const std::string& device_serial = "") const;
        
        // Logging helpers
        void LogAction(const std::string& action, int device_index, float intensity, float duration, const std::string& reason) const;
    };

}

