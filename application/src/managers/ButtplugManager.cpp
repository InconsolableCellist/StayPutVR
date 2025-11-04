#include "ButtplugManager.hpp"
#include <thread>
#include <sstream>
#include <algorithm>

namespace StayPutVR {

    ButtplugManager::ButtplugManager()
        : config_(nullptr)
        , enabled_(false)
        , user_agreement_(false)
        , connected_(false)
        , server_ready_(false)
        , next_message_id_(1)
        , last_action_time_(std::chrono::steady_clock::now())
        , last_ping_time_(std::chrono::steady_clock::now())
        , last_error_("")
        , action_callback_(nullptr)
    {
    }

    ButtplugManager::~ButtplugManager() {
        Shutdown();
    }

    bool ButtplugManager::Initialize(Config* config) {
        if (!config) {
            SetError("Invalid configuration provided");
            return false;
        }

        config_ = config;
        enabled_ = config_->buttplug_enabled;
        user_agreement_ = config_->buttplug_user_agreement;

        // Create WebSocket client
        ws_client_ = std::make_unique<WebSocketClient>();
        
        // Set up callbacks
        ws_client_->SetOnConnectedCallback([this]() { OnWebSocketConnected(); });
        ws_client_->SetOnDisconnectedCallback([this](const std::string& reason) { OnWebSocketDisconnected(reason); });
        ws_client_->SetOnMessageCallback([this](const std::string& message) { OnWebSocketMessage(message); });
        ws_client_->SetOnErrorCallback([this](const std::string& error) { OnWebSocketError(error); });

        Logger::Info("ButtplugManager initialized");
        return true;
    }

    void ButtplugManager::Shutdown() {
        if (connected_) {
            StopAllDevices();
        }
        Disconnect();
        enabled_ = false;
        user_agreement_ = false;
        ws_client_.reset();
        config_ = nullptr;
        Logger::Info("ButtplugManager shutdown");
    }

    void ButtplugManager::Update() {
        if (!ws_client_) return;
        
        // Process WebSocket messages
        ws_client_->Update();
        
        // Send periodic ping if connected (TODO: use MaxPingTime (uint, ms) from ServerInfo response)
        if (connected_ && server_ready_) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_ping_time_);
            
            if (elapsed.count() >= PING_INTERVAL_SECONDS) {
                SendPing();
                last_ping_time_ = now;
            }
        }
    }

    bool ButtplugManager::Connect() {
        if (!ValidateConfiguration()) {
            SetError("Invalid configuration - cannot connect");
            return false;
        }

        if (connected_) {
            Logger::Info("Already connected to Buttplug/Intiface");
            return true;
        }

        // Build WebSocket URL
        std::string url = "ws://" + config_->buttplug_server_address + ":" + 
                         std::to_string(config_->buttplug_server_port);

        Logger::Info("Connecting to Buttplug/Intiface at " + url + "...");
        
        if (ws_client_->Connect(url)) {
            // Connection initiated, wait for OnWebSocketConnected callback
            return true;
        } else {
            SetError("Failed to connect: " + ws_client_->GetLastError());
            return false;
        }
    }

    void ButtplugManager::Disconnect() {
        if (ws_client_ && connected_) {
            Logger::Info("Disconnecting from Buttplug/Intiface");
            if (server_ready_) {
                StopAllDevices();
            }
            ws_client_->Disconnect();
            connected_ = false;
            server_ready_ = false;
            
            {
                std::lock_guard<std::mutex> lock(devices_mutex_);
                available_devices_.clear();
            }
            
            {
                std::lock_guard<std::mutex> lock(zone_state_mutex_);
                current_zone_state_.clear();
            }
        }
    }

    bool ButtplugManager::IsConnected() const {
        return connected_ && ws_client_ && ws_client_->IsConnected() && server_ready_;
    }

    bool ButtplugManager::ValidateConfiguration() const {
        if (!config_) return false;
        
        // Check if at least one device index is configured
        bool has_device = false;
        for (const auto& idx : config_->buttplug_device_indices) {
            if (idx >= 0) {  // -1 means not configured, 0+ are valid device indices
                has_device = true;
                break;
            }
        }
        
        // Note: device indices might not be known until after connection
        // So we'll be lenient here and only require server address and port
        return !config_->buttplug_server_address.empty() &&
               config_->buttplug_server_port > 0 &&
               config_->buttplug_server_port < 65536;
    }

    bool ButtplugManager::IsFullyConfigured() const {
        return ValidateConfiguration() && 
               config_->buttplug_enabled && 
               config_->buttplug_user_agreement;
    }

    void ButtplugManager::TriggerSafeZoneActions(const std::string& device_serial) {
        if (!IsEnabled()) {
            return;
        }

        if (!IsConnected()) {
            Logger::Warning("Buttplug not connected, skipping safe zone actions");
            return;
        }

        if (!CanTriggerAction()) {
            Logger::Debug("Rate limit active, skipping safe zone actions");
            return;
        }

        // If safe zone vibration is enabled, start vibrating
        if (config_->buttplug_safe_zone_enabled) {
            Logger::Info("Triggering Buttplug safe zone actions for device: " + 
                       (device_serial.empty() ? "ALL" : device_serial));
            ExecuteZoneAction(ButtplugZoneType::SAFE, device_serial);
        } else {
            // Safe zone vibration is disabled, stop vibration
            ClearZoneState(device_serial);
        }
    }
    
    void ButtplugManager::ClearZoneState(const std::string& device_serial) {
        if (!IsEnabled()) {
            return;
        }

        if (!IsConnected()) {
            return;
        }

        Logger::Info("Clearing zone state for device: " + 
                   (device_serial.empty() ? "ALL" : device_serial) + " - stopping vibration");

        // Clear zone state
        {
            std::lock_guard<std::mutex> lock(zone_state_mutex_);
            if (device_serial.empty() || device_serial == "ALL") {
                current_zone_state_.clear();
            } else {
                current_zone_state_.erase(device_serial);
            }
        }

        // Stop vibration on all configured devices
        auto device_indices = GetEnabledDeviceIndices(device_serial);
        if (!device_indices.empty()) {
            StopVibrationMulti(device_indices);
        }
    }

    void ButtplugManager::TriggerWarningActions(const std::string& device_serial) {
        if (!IsEnabled()) {
            return;
        }

        if (!IsConnected()) {
            Logger::Warning("Buttplug not connected, skipping warning actions");
            return;
        }

        if (!CanTriggerAction()) {
            Logger::Debug("Rate limit active, skipping warning actions");
            return;
        }

        // If warning zone vibration is enabled, start vibrating
        if (config_->buttplug_warning_zone_enabled) {
            Logger::Info("Triggering Buttplug warning actions for device: " + 
                       (device_serial.empty() ? "ALL" : device_serial));
            ExecuteZoneAction(ButtplugZoneType::WARNING, device_serial);
        } else {
            // Warning zone vibration is disabled, stop vibration
            Logger::Info("Entering warning zone with vibration disabled - stopping vibration for device: " + 
                       (device_serial.empty() ? "ALL" : device_serial));
            ClearZoneState(device_serial);
        }
    }

    void ButtplugManager::TriggerDisobedienceActions(const std::string& device_serial) {
        if (!IsEnabled()) {
            return;
        }

        if (!IsConnected()) {
            Logger::Warning("Buttplug not connected, skipping disobedience actions");
            return;
        }

        if (!CanTriggerAction()) {
            Logger::Debug("Rate limit active, skipping disobedience actions");
            return;
        }

        // If disobedience zone vibration is enabled, start vibrating
        if (config_->buttplug_disobedience_zone_enabled) {
            Logger::Info("Triggering Buttplug disobedience actions for device: " + 
                       (device_serial.empty() ? "ALL" : device_serial));
            ExecuteZoneAction(ButtplugZoneType::DISOBEDIENCE, device_serial);
        } else {
            // Disobedience zone vibration is disabled, stop vibration
            Logger::Info("Entering disobedience zone with vibration disabled - stopping vibration for device: " + 
                       (device_serial.empty() ? "ALL" : device_serial));
            ClearZoneState(device_serial);
        }
    }

    void ButtplugManager::TestActions() {
        if (!IsEnabled()) {
            Logger::Info("Buttplug not enabled, skipping test");
            return;
        }

        if (!IsConnected()) {
            Logger::Warning("Buttplug not connected, skipping test");
            return;
        }

        Logger::Info("Testing Buttplug continuous vibration (disobedience intensity)");
        
        // Test continuous vibration on all configured devices at disobedience intensity
        float intensity = config_->buttplug_use_individual_disobedience_intensities ? 
                         config_->buttplug_individual_disobedience_intensities[0] : 
                         config_->buttplug_master_disobedience_intensity;
        
        auto device_indices = GetEnabledDeviceIndices();
        for (int device_index : device_indices) {
            SendVibrateContinuous(device_index, intensity, "Test");
        }
    }

    void ButtplugManager::StopAllDevices() {
        if (!IsConnected()) {
            return;
        }

        Logger::Info("Stopping all Buttplug devices");
        SendStopAllDevices();
    }

    void ButtplugManager::SendVibrate(int device_index, float intensity, float duration, const std::string& reason) {
        if (!IsConnected()) {
            Logger::Warning("Buttplug not connected, cannot send vibration");
            return;
        }

        // Clamp intensity to 0.0-1.0 range
        float clamped_intensity = (std::max)(0.0f, (std::min)(1.0f, intensity));
        
        LogAction("Vibrate", device_index, clamped_intensity, duration, reason);
        
        SendScalarCmd(device_index, clamped_intensity, "Vibrate");
    }

    void ButtplugManager::SendVibrateMulti(const std::vector<int>& device_indices, float intensity, float duration, const std::string& reason) {
        if (!IsConnected()) {
            Logger::Warning("Buttplug not connected, cannot send vibration");
            return;
        }

        for (int device_index : device_indices) {
            SendVibrate(device_index, intensity, duration, reason);
        }
    }

    void ButtplugManager::SendVibrateContinuous(int device_index, float intensity, const std::string& reason) {
        if (!IsConnected()) {
            Logger::Warning("Buttplug not connected, cannot send vibration");
            return;
        }

        // Clamp intensity to 0.0-1.0 range
        float clamped_intensity = (std::max)(0.0f, (std::min)(1.0f, intensity));
        
        LogAction("Vibrate (Continuous)", device_index, clamped_intensity, 0.0f, reason);
        SendScalarCmd(device_index, clamped_intensity, "Vibrate");
    }

    void ButtplugManager::StopVibration(int device_index) {
        if (!IsConnected()) {
            return;
        }

        Logger::Info("Stopping vibration on device " + std::to_string(device_index));
        SendScalarCmd(device_index, 0.0f, "Vibrate");
    }

    void ButtplugManager::StopVibrationMulti(const std::vector<int>& device_indices) {
        if (!IsConnected()) {
            return;
        }

        for (int device_index : device_indices) {
            StopVibration(device_index);
        }
    }

    bool ButtplugManager::RequestDeviceList() {
        if (!IsConnected()) {
            Logger::Warning("Buttplug not connected, cannot request device list");
            return false;
        }

        nlohmann::json message = nlohmann::json::array({
            {
                {"RequestDeviceList", {
                    {"Id", GetNextMessageId()}
                }}
            }
        });

        std::string message_str = message.dump();
        Logger::Debug("Sending RequestDeviceList: " + message_str);
        
        return ws_client_->SendText(message_str);
    }

    std::vector<ButtplugDeviceInfo> ButtplugManager::GetAvailableDevices() const {
        std::lock_guard<std::mutex> lock(devices_mutex_);
        std::vector<ButtplugDeviceInfo> devices;
        for (const auto& [index, info] : available_devices_) {
            devices.push_back(info);
        }
        return devices;
    }

    std::string ButtplugManager::GetConnectionStatus() const {
        if (!ws_client_) {
            return "Not initialized";
        }
        
        if (!connected_) {
            return "Disconnected";
        }
        
        if (!server_ready_) {
            return "Connecting...";
        }
        
        std::lock_guard<std::mutex> lock(devices_mutex_);
        return "Connected (" + std::to_string(available_devices_.size()) + " devices)";
    }

    bool ButtplugManager::CanTriggerAction() const {
        std::lock_guard<std::mutex> lock(rate_limit_mutex_);
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_action_time_);
        return elapsed.count() >= RATE_LIMIT_MILLISECONDS;
    }

    float ButtplugManager::ConvertIntensityToAPI(float normalized_intensity) {
        // Buttplug uses 0.0-1.0 range, so this is a passthrough
        return (std::max)(0.0f, (std::min)(1.0f, normalized_intensity));
    }

    int ButtplugManager::ConvertDurationToMilliseconds(float duration_seconds) {
        return static_cast<int>(duration_seconds * 1000.0f);
    }

    // ========== Private Methods ==========

    void ButtplugManager::OnWebSocketConnected() {
        Logger::Info("WebSocket connected to Buttplug/Intiface");
        connected_ = true;
        
        // Send RequestServerInfo as the first message (required by Buttplug spec)
        if (!SendRequestServerInfo()) {
            SetError("Failed to send RequestServerInfo");
            Disconnect();
        }
    }

    void ButtplugManager::OnWebSocketDisconnected(const std::string& reason) {
        Logger::Info("WebSocket disconnected from Buttplug/Intiface: " + reason);
        connected_ = false;
        server_ready_ = false;
        
        std::lock_guard<std::mutex> lock(devices_mutex_);
        available_devices_.clear();
    }

    void ButtplugManager::OnWebSocketMessage(const std::string& message) {
        Logger::Debug("Received Buttplug message: " + message);
        
        try {
            nlohmann::json j = nlohmann::json::parse(message);
            
            if (!j.is_array() || j.empty()) {
                Logger::Warning("Invalid Buttplug message format (not an array or empty)");
                return;
            }
            
            for (const auto& msg : j) {
                if (msg.contains("ServerInfo")) {
                    HandleServerInfo(msg);
                } else if (msg.contains("DeviceAdded")) {
                    HandleDeviceAdded(msg);
                } else if (msg.contains("DeviceRemoved")) {
                    HandleDeviceRemoved(msg);
                } else if (msg.contains("DeviceList")) {
                    HandleDeviceList(msg);
                } else if (msg.contains("Ok")) {
                    HandleOk(msg);
                } else if (msg.contains("Error")) {
                    HandleError(msg);
                } else {
                    Logger::Debug("Unknown Buttplug message type");
                }
            }
        } catch (const nlohmann::json::exception& e) {
            Logger::Error("Failed to parse Buttplug message: " + std::string(e.what()));
        }
    }

    void ButtplugManager::OnWebSocketError(const std::string& error) {
        Logger::Error("Buttplug WebSocket error: " + error);
        SetError(error);
    }

    void ButtplugManager::SetError(const std::string& error) {
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_ = error;
        Logger::Error("ButtplugManager error: " + error);
    }

    bool ButtplugManager::CheckRateLimit() {
        std::lock_guard<std::mutex> lock(rate_limit_mutex_);
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_action_time_);
        return elapsed.count() >= RATE_LIMIT_MILLISECONDS;
    }

    void ButtplugManager::UpdateRateLimit() {
        std::lock_guard<std::mutex> lock(rate_limit_mutex_);
        last_action_time_ = std::chrono::steady_clock::now();
    }

    void ButtplugManager::ExecuteZoneAction(ButtplugZoneType zone_type, const std::string& device_serial) {
        // Check if the zone has actually changed for this device
        bool zone_changed = false;
        {
            std::lock_guard<std::mutex> lock(zone_state_mutex_);
            auto it = current_zone_state_.find(device_serial);
            if (it != current_zone_state_.end() && it->second == zone_type) {
                // Already in this zone, no action needed
                return;
            }
            zone_changed = true;
            // Update zone state
            current_zone_state_[device_serial] = zone_type;
        }
        
        float intensity = 0.0f;
        std::string zone_name;
        
        switch (zone_type) {
            case ButtplugZoneType::SAFE:
                intensity = config_->buttplug_use_individual_safe_intensities ? 
                           config_->buttplug_individual_safe_intensities[0] : 
                           config_->buttplug_master_safe_intensity;
                zone_name = "Safe";
                break;
                
            case ButtplugZoneType::WARNING:
                intensity = config_->buttplug_use_individual_warning_intensities ? 
                           config_->buttplug_individual_warning_intensities[0] : 
                           config_->buttplug_master_warning_intensity;
                zone_name = "Warning";
                break;
                
            case ButtplugZoneType::DISOBEDIENCE:
                intensity = config_->buttplug_use_individual_disobedience_intensities ? 
                           config_->buttplug_individual_disobedience_intensities[0] : 
                           config_->buttplug_master_disobedience_intensity;
                zone_name = "Disobedience";
                break;
        }
        
        // Get device indices before starting new vibration
        auto device_indices = GetEnabledDeviceIndices(device_serial);
        if (!device_indices.empty()) {
            // Send new intensity directly - Buttplug protocol allows updating intensity without stopping
            Logger::Info("Zone changed to " + zone_name + " for device " + device_serial + 
                        " - setting continuous vibration at intensity " + std::to_string(intensity));
            for (int device_index : device_indices) {
                SendVibrateContinuous(device_index, intensity, zone_name);
            }
        }
        
        UpdateRateLimit();
    }

    bool ButtplugManager::ValidateConnectionParameters() const {
        return !config_->buttplug_server_address.empty() &&
               config_->buttplug_server_port > 0 &&
               config_->buttplug_server_port < 65536;
    }

    bool ButtplugManager::SendRequestServerInfo() {
        // ALL Buttplug messages must be wrapped in an array
        nlohmann::json message = nlohmann::json::array({
            {
                {"RequestServerInfo", {
                    {"Id", GetNextMessageId()},
                    {"ClientName", "StayPutVR"},
                    {"MessageVersion", 3}  // Buttplug spec version 3
                }}
            }
        });

        Logger::Debug("Sending RequestServerInfo: " + message.dump());
        
        return ws_client_->SendText(message.dump());
    }

    bool ButtplugManager::SendStartScanning() {
        nlohmann::json message = nlohmann::json::array({
            {
                {"StartScanning", {
                    {"Id", GetNextMessageId()}
                }}
            }
        });

        std::string message_str = message.dump();
        Logger::Debug("Sending StartScanning: " + message_str);
        
        return ws_client_->SendText(message_str);
    }

    bool ButtplugManager::SendStopScanning() {
        // ALL Buttplug messages must be wrapped in an array
        nlohmann::json message = nlohmann::json::array({
            {
                {"StopScanning", {
                    {"Id", GetNextMessageId()}
                }}
            }
        });

        std::string message_str = message.dump();
        Logger::Debug("Sending StopScanning: " + message_str);
        
        return ws_client_->SendText(message_str);
    }

    bool ButtplugManager::SendScalarCmd(int device_index, float scalar, const std::string& actuator_type) {
        nlohmann::json message = nlohmann::json::array({
            {
                {"ScalarCmd", {
                    {"Id", GetNextMessageId()},
                    {"DeviceIndex", device_index},
                    {"Scalars", nlohmann::json::array({
                        {
                            {"Index", 0},
                            {"Scalar", scalar},
                            {"ActuatorType", actuator_type}
                        }
                    })}
                }}
            }
        });

        std::string message_str = message.dump();
        Logger::Debug("Sending ScalarCmd: " + message_str);
        
        return ws_client_->SendText(message_str);
    }

    bool ButtplugManager::SendStopDeviceCmd(int device_index) {
        nlohmann::json message = nlohmann::json::array({
            {
                {"StopDeviceCmd", {
                    {"Id", GetNextMessageId()},
                    {"DeviceIndex", device_index}
                }}
            }
        });

        std::string message_str = message.dump();
        Logger::Debug("Sending StopDeviceCmd: " + message_str);
        
        return ws_client_->SendText(message_str);
    }

    bool ButtplugManager::SendStopAllDevices() {
        nlohmann::json message = nlohmann::json::array({
            {
                {"StopAllDevices", {
                    {"Id", GetNextMessageId()}
                }}
            }
        });

        std::string message_str = message.dump();
        Logger::Debug("Sending StopAllDevices: " + message_str);
        
        return ws_client_->SendText(message_str);
    }

    bool ButtplugManager::SendPing() {
        nlohmann::json message = nlohmann::json::array({
            {
                {"Ping", {
                    {"Id", GetNextMessageId()}
                }}
            }
        });

        std::string message_str = message.dump();
        Logger::Debug("Sending Ping: " + message_str);
        
        return ws_client_->SendText(message_str);
    }

    void ButtplugManager::HandleServerInfo(const nlohmann::json& message) {
        try {
            const auto& server_info = message["ServerInfo"];
            std::string server_name = server_info.value("ServerName", "Unknown");
            int message_version = server_info.value("MessageVersion", 0);
            
            Logger::Info("Connected to Buttplug server: " + server_name + 
                        " (Protocol v" + std::to_string(message_version) + ")");
            
            server_ready_ = true;
            
            RequestDeviceList();
        } catch (const nlohmann::json::exception& e) {
            Logger::Error("Failed to parse ServerInfo: " + std::string(e.what()));
        }
    }

    void ButtplugManager::HandleDeviceAdded(const nlohmann::json& message) {
        try {
            const auto& device = message["DeviceAdded"];
            AddDevice(device);
            
        } catch (const nlohmann::json::exception& e) {
            Logger::Error("Failed to parse DeviceAdded: " + std::string(e.what()));
        }
    }

    void ButtplugManager::HandleDeviceRemoved(const nlohmann::json& message) {
        try {
            const auto& device_removed = message["DeviceRemoved"];
            int device_index = device_removed.value("DeviceIndex", -1);
            
            if (device_index >= 0) {
                RemoveDevice(device_index);
            }
            
        } catch (const nlohmann::json::exception& e) {
            Logger::Error("Failed to parse DeviceRemoved: " + std::string(e.what()));
        }
    }

    void ButtplugManager::HandleDeviceList(const nlohmann::json& message) {
        try {
            const auto& device_list = message["DeviceList"];
            const auto& devices = device_list["Devices"];
            
            Logger::Info("Received device list with " + std::to_string(devices.size()) + " devices");
            
            std::lock_guard<std::mutex> lock(devices_mutex_);
            available_devices_.clear();
            
            for (const auto& device : devices) {
                AddDevice(device);
            }
            
        } catch (const nlohmann::json::exception& e) {
            Logger::Error("Failed to parse DeviceList: " + std::string(e.what()));
        }
    }

    void ButtplugManager::HandleOk(const nlohmann::json& message) {
        try {
            const auto& ok = message["Ok"];
            int msg_id = ok.value("Id", 0);
            Logger::Debug("Received Ok for message ID " + std::to_string(msg_id));
            
        } catch (const nlohmann::json::exception& e) {
            Logger::Error("Failed to parse Ok: " + std::string(e.what()));
        }
    }

    void ButtplugManager::HandleError(const nlohmann::json& message) {
        try {
            const auto& error = message["Error"];
            int msg_id = error.value("Id", 0);
            std::string error_message = error.value("ErrorMessage", "Unknown error");
            int error_code = error.value("ErrorCode", 0);
            
            Logger::Error("Received Error for message ID " + std::to_string(msg_id) + 
                         ": " + error_message + " (Code: " + std::to_string(error_code) + ")");
            
            SetError(error_message);
            
        } catch (const nlohmann::json::exception& e) {
            Logger::Error("Failed to parse Error: " + std::string(e.what()));
        }
    }

    uint32_t ButtplugManager::GetNextMessageId() {
        return next_message_id_++;
    }

    void ButtplugManager::AddDevice(const nlohmann::json& device_json) {
        try {
            int device_index = device_json.value("DeviceIndex", -1);
            std::string device_name = device_json.value("DeviceName", "Unknown");
            
            if (device_index < 0) {
                Logger::Warning("Invalid device index in AddDevice");
                return;
            }
            
            ButtplugDeviceInfo info;
            info.device_index = device_index;
            info.device_name = device_name;
            info.supports_vibration = false;
            info.vibration_feature_count = 0;
            
            // Check if device supports ScalarCmd with Vibrate actuator
            if (device_json.contains("DeviceMessages")) {
                const auto& messages = device_json["DeviceMessages"];
                
                if (messages.contains("ScalarCmd")) {
                    const auto& scalar_cmd = messages["ScalarCmd"];
                    if (scalar_cmd.is_array()) {
                        for (const auto& actuator : scalar_cmd) {
                            std::string actuator_type = actuator.value("ActuatorType", "");
                            if (actuator_type == "Vibrate") {
                                info.supports_vibration = true;
                                info.vibration_feature_count++;
                            }
                        }
                    }
                }
            }
            
            {
                std::lock_guard<std::mutex> lock(devices_mutex_);
                available_devices_[device_index] = info;
            }
            
            Logger::Info("Device added: [" + std::to_string(device_index) + "] " + 
                        device_name + " (Vibration: " + 
                        (info.supports_vibration ? "Yes" : "No") + ")");
            
        } catch (const nlohmann::json::exception& e) {
            Logger::Error("Failed to add device: " + std::string(e.what()));
        }
    }

    void ButtplugManager::RemoveDevice(int device_index) {
        std::lock_guard<std::mutex> lock(devices_mutex_);
        auto it = available_devices_.find(device_index);
        if (it != available_devices_.end()) {
            Logger::Info("Device removed: [" + std::to_string(device_index) + "] " + 
                        it->second.device_name);
            available_devices_.erase(it);
        }
    }

    void ButtplugManager::VibrateConfiguredDevices(float intensity, float duration, const std::string& reason) {
        auto device_indices = GetEnabledDeviceIndices();
        
        if (device_indices.empty()) {
            Logger::Warning("No configured Buttplug devices to vibrate");
            return;
        }
        
        SendVibrateMulti(device_indices, intensity, duration, reason);
    }

    std::vector<int> ButtplugManager::GetEnabledDeviceIndices(const std::string& device_serial) const {
        std::vector<int> indices;
        
        // If device_serial is empty or "ALL", return all configured devices
        if (device_serial.empty() || device_serial == "ALL") {
            for (size_t i = 0; i < config_->buttplug_device_indices.size(); ++i) {
                int idx = config_->buttplug_device_indices[i];
                if (idx >= 0) {  // -1 means not configured, 0+ are valid device indices
                    indices.push_back(idx);
                }
            }
            return indices;
        }
        
        // Check if this VR device has vibration mappings in device_vibration_ids
        auto vibration_it = config_->device_vibration_ids.find(device_serial);
        if (vibration_it != config_->device_vibration_ids.end()) {
            const auto& vibration_enabled = vibration_it->second;
            
            // Return only the buttplug device indices that are enabled for this VR device
            for (size_t i = 0; i < vibration_enabled.size() && i < config_->buttplug_device_indices.size(); ++i) {
                if (vibration_enabled[i] && config_->buttplug_device_indices[i] >= 0) {
                    indices.push_back(config_->buttplug_device_indices[i]);
                }
            }
        }
        
        return indices;
    }

    void ButtplugManager::LogAction(const std::string& action, int device_index, float intensity, float duration, const std::string& reason) const {
        std::stringstream ss;
        ss << "Buttplug " << action << ": Device=" << device_index 
           << " Intensity=" << intensity 
           << " Duration=" << duration << "s";
        if (!reason.empty()) {
            ss << " Reason=" << reason;
        }
        Logger::Info(ss.str());
    }

}

