#include "PiShockWebSocketManager.hpp"
#include <thread>
#include <sstream>
#include <algorithm>

namespace StayPutVR {

    PiShockWebSocketManager::PiShockWebSocketManager()
        : config_(nullptr)
        , enabled_(false)
        , user_agreement_(false)
        , connected_(false)
        , last_action_time_(std::chrono::steady_clock::now())
        , last_shock_time_(std::chrono::steady_clock::now())
        , last_ping_time_(std::chrono::steady_clock::now())
        , last_error_("")
        , action_callback_(nullptr)
    {
    }

    PiShockWebSocketManager::~PiShockWebSocketManager() {
        Shutdown();
    }

    bool PiShockWebSocketManager::Initialize(Config* config) {
        if (!config) {
            SetError("Invalid configuration provided");
            return false;
        }

        config_ = config;
        enabled_ = config_->pishock_enabled;
        user_agreement_ = config_->pishock_user_agreement;

        // Create WebSocket client
        ws_client_ = std::make_unique<WebSocketClient>();
        
        // Set up callbacks
        ws_client_->SetOnConnectedCallback([this]() { OnWebSocketConnected(); });
        ws_client_->SetOnDisconnectedCallback([this](const std::string& reason) { OnWebSocketDisconnected(reason); });
        ws_client_->SetOnMessageCallback([this](const std::string& message) { OnWebSocketMessage(message); });
        ws_client_->SetOnErrorCallback([this](const std::string& error) { OnWebSocketError(error); });

        Logger::Info("PiShockWebSocketManager initialized");
        return true;
    }

    void PiShockWebSocketManager::Shutdown() {
        Disconnect();
        enabled_ = false;
        user_agreement_ = false;
        ws_client_.reset();
        config_ = nullptr;
        Logger::Info("PiShockWebSocketManager shutdown");
    }

    void PiShockWebSocketManager::Update() {
        if (!ws_client_) return;
        
        // Process WebSocket messages
        ws_client_->Update();
        
        // Send periodic ping if connected
        if (connected_) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_ping_time_);
            
            if (elapsed.count() >= PING_INTERVAL_SECONDS) {
                SendPing();
                last_ping_time_ = now;
            }
        }
    }

    bool PiShockWebSocketManager::Connect() {
        if (!ValidateConfiguration()) {
            SetError("Invalid configuration - cannot connect");
            return false;
        }

        if (connected_) {
            Logger::Info("Already connected to PiShock WebSocket");
            return true;
        }

        // Fetch User ID if not already cached
        if (config_->pishock_user_id == 0) {
            Logger::Info("Fetching PiShock User ID...");
            if (!FetchUserId()) {
                SetError("Failed to fetch User ID - check credentials");
                return false;
            }
            Logger::Info("User ID fetched successfully: " + std::to_string(config_->pishock_user_id));
        }

        // Build WebSocket URL for v2 API
        std::string url = "wss://broker.pishock.com/v2?Username=" + 
                         config_->pishock_username + 
                         "&ApiKey=" + config_->pishock_api_key;

        Logger::Info("Connecting to PiShock WebSocket v2...");
        
        if (ws_client_->Connect(url)) {
            // Connection successful, but we wait for OnWebSocketConnected callback
            return true;
        } else {
            SetError("Failed to connect: " + ws_client_->GetLastError());
            return false;
        }
    }

    void PiShockWebSocketManager::Disconnect() {
        if (ws_client_ && connected_) {
            Logger::Info("Disconnecting from PiShock WebSocket");
            ws_client_->Disconnect();
            connected_ = false;
        }
    }

    bool PiShockWebSocketManager::IsConnected() const {
        return connected_ && ws_client_ && ws_client_->IsConnected();
    }

    bool PiShockWebSocketManager::ValidateConfiguration() const {
        if (!config_) return false;
        
        // Check if at least one shocker ID is configured
        bool has_shocker_id = false;
        for (const auto& id : config_->pishock_shocker_ids) {
            if (id != 0) {
                has_shocker_id = true;
                break;
            }
        }
        
        // User ID is fetched automatically, so we don't require it in validation
        return !config_->pishock_username.empty() &&
               !config_->pishock_api_key.empty() &&
               !config_->pishock_client_id.empty() &&
               has_shocker_id;
    }

    bool PiShockWebSocketManager::IsFullyConfigured() const {
        return ValidateConfiguration() && 
               config_->pishock_enabled && 
               config_->pishock_user_agreement;
    }

    void PiShockWebSocketManager::TriggerDisobedienceActions(const std::string& device_serial) {
        if (!IsEnabled()) {
            Logger::Info("PiShock WebSocket not enabled, skipping disobedience actions");
            return;
        }

        if (!IsConnected()) {
            Logger::Warning("PiShock WebSocket not connected, skipping disobedience actions");
            return;
        }

        if (!CanTriggerAction()) {
            Logger::Info("Rate limit active, skipping disobedience actions");
            return;
        }

        Logger::Info("Triggering PiShock WebSocket disobedience actions for device: " + 
                   (device_serial.empty() ? "ALL" : device_serial));

        // Execute configured disobedience actions
        if (config_->pishock_disobedience_beep) {
            SendBeepMulti(1, "Disobedience - Beep", device_serial);
        }

        if (config_->pishock_disobedience_vibrate) {
            int duration = ConvertDurationToAPI(config_->pishock_disobedience_duration);
            SendVibrateMulti(duration, "Disobedience - Vibrate", device_serial);
        }

        if (config_->pishock_disobedience_shock) {
            int duration = ConvertDurationToAPI(config_->pishock_disobedience_duration);
            SendShockMulti(duration, "Disobedience - Shock", device_serial);
        }
    }

    void PiShockWebSocketManager::TriggerWarningActions(const std::string& device_serial) {
        if (!IsEnabled()) {
            Logger::Info("PiShock WebSocket not enabled, skipping warning actions");
            return;
        }

        if (!IsConnected()) {
            Logger::Warning("PiShock WebSocket not connected, skipping warning actions");
            return;
        }

        if (!CanTriggerAction()) {
            Logger::Info("Rate limit active, skipping warning actions");
            return;
        }

        Logger::Info("Triggering PiShock WebSocket warning actions for device: " + 
                   (device_serial.empty() ? "ALL" : device_serial));

        // Execute warning actions (typically lighter than disobedience)
        SendBeep(1, "Warning - Beep");
        
        if (config_->pishock_disobedience_vibrate) {
            int intensity = (std::max)(1, ConvertIntensityToAPI(config_->pishock_disobedience_intensity) / 2);
            SendVibrate(intensity, 1000, "Warning - Vibrate");
        }
    }

    void PiShockWebSocketManager::TestActions() {
        if (!IsEnabled()) {
            SetError("PiShock WebSocket not enabled");
            return;
        }

        if (!IsConnected()) {
            SetError("PiShock WebSocket not connected");
            return;
        }

        if (!ValidateConfiguration()) {
            SetError("PiShock WebSocket configuration invalid");
            return;
        }

        Logger::Info("Testing configured PiShock WebSocket out-of-bounds actions...");
        
        // Test the actual configured disobedience actions
        TriggerDisobedienceActions("TEST");
    }

    void PiShockWebSocketManager::SendBeep(int duration, const std::string& reason) {
        PiShockWSActionData action;
        action.type = PiShockWSActionType::BEEP;
        action.intensity = 0; // Beeps don't use intensity
        action.duration = (std::max)(1, (std::min)(15, duration)) * 1000; // Convert to milliseconds
        action.reason = reason;

        ExecuteActionAsync(action);
    }

    void PiShockWebSocketManager::SendVibrate(int intensity, int duration, const std::string& reason) {
        if (!ValidateActionParameters(intensity, duration)) {
            SetError("Invalid vibrate parameters");
            return;
        }

        PiShockWSActionData action;
        action.type = PiShockWSActionType::VIBRATE;
        action.intensity = intensity;
        action.duration = duration; // Already in milliseconds
        action.reason = reason;

        ExecuteActionAsync(action);
    }

    void PiShockWebSocketManager::SendShock(int intensity, int duration, const std::string& reason) {
        if (!ValidateActionParameters(intensity, duration)) {
            SetError("Invalid shock parameters");
            return;
        }

        if (!CheckShockCooldown()) {
            std::string cooldown_msg = "Shock cooldown active (waiting " + 
                                      std::to_string((int)config_->shock_cooldown_seconds) + "s between shocks)";
            Logger::Info(cooldown_msg);
            SetError(cooldown_msg);
            if (action_callback_) {
                action_callback_("Shock", false, cooldown_msg);
            }
            return;
        }

        PiShockWSActionData action;
        action.type = PiShockWSActionType::SHOCK;
        action.intensity = intensity;
        action.duration = duration; // Already in milliseconds
        action.reason = reason;

        ExecuteActionAsync(action);
    }

    std::string PiShockWebSocketManager::GetConnectionStatus() const {
        if (!config_) return "Not initialized";
        if (!config_->pishock_enabled) return "Disabled";
        if (!config_->pishock_user_agreement) return "User agreement required";
        if (!ValidateConfiguration()) return "Configuration incomplete";
        if (!IsConnected()) return "Disconnected";
        return "Connected";
    }

    bool PiShockWebSocketManager::CanTriggerAction() const {
        std::lock_guard<std::mutex> lock(rate_limit_mutex_);
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_action_time_);
        return elapsed.count() >= RATE_LIMIT_SECONDS;
    }

    int PiShockWebSocketManager::ConvertIntensityToAPI(float normalized_intensity) {
        return (std::max)(1, (std::min)(100, static_cast<int>(normalized_intensity * 100.0f)));
    }

    int PiShockWebSocketManager::ConvertDurationToAPI(float duration_seconds) {
        // Convert seconds to milliseconds
        return (std::max)(1, (std::min)(15000, static_cast<int>(duration_seconds * 1000.0f)));
    }

    void PiShockWebSocketManager::SetError(const std::string& error) {
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_ = error;
        Logger::Error("PiShockWebSocketManager Error: " + error);
    }

    bool PiShockWebSocketManager::CheckRateLimit() {
        std::lock_guard<std::mutex> lock(rate_limit_mutex_);
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_action_time_);
        
        if (elapsed.count() >= RATE_LIMIT_SECONDS) {
            last_action_time_ = now;
            return true;
        }
        return false;
    }

    void PiShockWebSocketManager::UpdateRateLimit() {
        std::lock_guard<std::mutex> lock(rate_limit_mutex_);
        last_action_time_ = std::chrono::steady_clock::now();
    }

    bool PiShockWebSocketManager::CheckShockCooldown() {
        if (!config_ || !config_->shock_cooldown_enabled) {
            return true;
        }
        
        std::lock_guard<std::mutex> lock(shock_cooldown_mutex_);
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_shock_time_);
        
        if (elapsed.count() >= config_->shock_cooldown_seconds) {
            last_shock_time_ = now;
            return true;
        }
        return false;
    }

    void PiShockWebSocketManager::UpdateShockCooldown() {
        std::lock_guard<std::mutex> lock(shock_cooldown_mutex_);
        last_shock_time_ = std::chrono::steady_clock::now();
    }

    void PiShockWebSocketManager::ExecuteAction(const PiShockWSActionData& action) {
        if (!ValidateCredentials()) {
            SetError("Invalid PiShock credentials");
            if (action_callback_) {
                action_callback_(ActionTypeToString(action.type), false, "Invalid credentials");
            }
            return;
        }

        if (!IsConnected()) {
            SetError("Not connected to WebSocket");
            if (action_callback_) {
                action_callback_(ActionTypeToString(action.type), false, "Not connected");
            }
            return;
        }

        if (!CheckRateLimit()) {
            SetError("Rate limit exceeded");
            if (action_callback_) {
                action_callback_(ActionTypeToString(action.type), false, "Rate limit exceeded");
            }
            return;
        }

        try {
            Logger::Info("Sending PiShock WebSocket " + ActionTypeToString(action.type) + 
                       " (Intensity: " + std::to_string(action.intensity) + 
                       ", Duration: " + std::to_string(action.duration) + "ms" +
                       ", Reason: " + action.reason + ")");

            bool success = SendPublishCommand(
                ActionTypeToMode(action.type),
                action.intensity,
                action.duration,
                "StayPutVR"
            );
            
            LogAction(action, success, success ? "Command sent" : "Failed to send command");
            
            if (action_callback_) {
                action_callback_(ActionTypeToString(action.type), success, 
                               success ? "Action sent successfully" : "Failed to send");
            }

        } catch (const std::exception& e) {
            std::string error = "PiShock WebSocket action failed: " + std::string(e.what());
            SetError(error);
            LogAction(action, false, error);
            
            if (action_callback_) {
                action_callback_(ActionTypeToString(action.type), false, error);
            }
        }
    }

    void PiShockWebSocketManager::ExecuteActionAsync(const PiShockWSActionData& action) {
        // Execute in separate thread to avoid blocking UI
        std::thread([this, action]() {
            ExecuteAction(action);
        }).detach();
    }

    bool PiShockWebSocketManager::ValidateCredentials() const {
        return config_ && 
               !config_->pishock_username.empty() &&
               !config_->pishock_api_key.empty() &&
               !config_->pishock_client_id.empty();
    }

    bool PiShockWebSocketManager::ValidateActionParameters(int intensity, int duration) const {
        return intensity >= 1 && intensity <= 100 &&
               duration >= 1 && duration <= 15000;
    }

    bool PiShockWebSocketManager::FetchUserId() {
        if (!config_) {
            Logger::Error("Config is null, cannot fetch User ID");
            return false;
        }

        try {
            // Build API URL
            std::string url = "https://auth.pishock.com/Auth/GetUserIfAPIKeyValid?apikey=" + 
                             config_->pishock_api_key + 
                             "&username=" + config_->pishock_username;

            Logger::Debug("Fetching User ID from: " + url);

            // Make HTTP GET request
            std::string response;
            std::map<std::string, std::string> headers;
            
            bool success = HttpClient::SendHttpRequest(
                url,
                "GET",
                headers,
                "",  // No body for GET request
                response
            );

            if (!success || response.empty()) {
                Logger::Error("Failed to fetch User ID from API");
                return false;
            }

            Logger::Debug("User ID API response: " + response);

            // Parse JSON response
            nlohmann::json json_response = nlohmann::json::parse(response);

            // Check if response contains user ID
            if (json_response.contains("UserId")) {
                if (json_response["UserId"].is_number()) {
                    config_->pishock_user_id = json_response["UserId"].get<int>();
                } else {
                    Logger::Error("User ID field has unexpected type (expected integer)");
                    return false;
                }
                
                Logger::Info("Successfully fetched User ID: " + std::to_string(config_->pishock_user_id));
                return true;
            } else {
                Logger::Error("User ID not found in API response");
                return false;
            }

        } catch (const std::exception& e) {
            Logger::Error("Failed to fetch User ID: " + std::string(e.what()));
            return false;
        }
    }

    void PiShockWebSocketManager::LogAction(const PiShockWSActionData& action, bool success, const std::string& response) const {
        std::stringstream log_msg;
        log_msg << "PiShock WebSocket " << ActionTypeToString(action.type) 
                << " (I:" << action.intensity << ", D:" << action.duration << "ms) "
                << (success ? "SUCCESS" : "FAILED");
        
        if (!action.reason.empty()) {
            log_msg << " - " << action.reason;
        }
        
        if (!success && !response.empty()) {
            log_msg << " - " << response;
        }
        
        Logger::Info(log_msg.str());
    }

    std::string PiShockWebSocketManager::ActionTypeToString(PiShockWSActionType type) const {
        switch (type) {
            case PiShockWSActionType::BEEP: return "Beep";
            case PiShockWSActionType::VIBRATE: return "Vibrate";
            case PiShockWSActionType::SHOCK: return "Shock";
            case PiShockWSActionType::STOP: return "Stop";
            default: return "Unknown";
        }
    }

    std::string PiShockWebSocketManager::ActionTypeToMode(PiShockWSActionType type) const {
        switch (type) {
            case PiShockWSActionType::BEEP: return "b";
            case PiShockWSActionType::VIBRATE: return "v";
            case PiShockWSActionType::SHOCK: return "s";
            case PiShockWSActionType::STOP: return "e";
            default: return "e";
        }
    }

    // WebSocket callbacks
    void PiShockWebSocketManager::OnWebSocketConnected() {
        connected_ = true;
        Logger::Info("PiShock WebSocket connected successfully");
        
        // Send initial ping
        SendPing();
        last_ping_time_ = std::chrono::steady_clock::now();
    }

    void PiShockWebSocketManager::OnWebSocketDisconnected(const std::string& reason) {
        connected_ = false;
        Logger::Warning("PiShock WebSocket disconnected: " + reason);
        SetError("Disconnected: " + reason);
    }

    void PiShockWebSocketManager::OnWebSocketMessage(const std::string& message) {
        try {
            nlohmann::json response = nlohmann::json::parse(message);
            
            // Log the response
            Logger::Debug("PiShock WebSocket response: " + message);
            
            // Check for errors
            if (response.contains("IsError") && response["IsError"].get<bool>()) {
                std::string error_msg = response.value("Message", "Unknown error");
                Logger::Error("PiShock WebSocket error response: " + error_msg);
                SetError(error_msg);
            }
            else if (response.contains("Message")) {
                std::string msg = response["Message"].get<std::string>();
                
                if (msg == "PONG") {
                    Logger::Debug("PiShock WebSocket PONG received");
                }
                else if (msg == "Publish successful.") {
                    Logger::Debug("PiShock WebSocket publish successful");
                }
                else {
                    Logger::Info("PiShock WebSocket message: " + msg);
                }
            }
            
        } catch (const std::exception& e) {
            Logger::Error("Failed to parse PiShock WebSocket message: " + std::string(e.what()));
        }
    }

    void PiShockWebSocketManager::OnWebSocketError(const std::string& error) {
        Logger::Error("PiShock WebSocket error: " + error);
        SetError(error);
    }

    // WebSocket protocol methods
    bool PiShockWebSocketManager::SendPing() {
        if (!ws_client_ || !connected_) {
            return false;
        }

        nlohmann::json ping_msg;
        ping_msg["Operation"] = "PING";
        ping_msg["Targets"] = nullptr;
        ping_msg["PublishCommands"] = nullptr;
        
        std::string msg = ping_msg.dump();
        Logger::Debug("Sending PiShock WebSocket PING: " + msg);
        
        return ws_client_->SendText(msg);
    }

    bool PiShockWebSocketManager::SendPublishCommand(
        const std::string& mode, 
        int intensity, 
        int duration, 
        const std::string& origin) {
        
        if (!ws_client_ || !connected_) {
            SetError("WebSocket not connected");
            return false;
        }

        try {
            // Get the channel target
            std::string target = GetChannelTarget();
            
            // Build the command body
            nlohmann::json body;
            body["id"] = config_->pishock_shocker_ids[0];  // Use first shocker for legacy single-device call 
            body["m"] = mode;
            body["i"] = intensity;
            body["d"] = duration;
            body["r"] = true;
            
            // Log metadata
            nlohmann::json log_data;
            log_data["u"] = config_->pishock_user_id;
            log_data["ty"] = "api";  // "sc" for ShareCode, "api" for direct API access
            log_data["w"] = false;
            log_data["h"] = false;
            log_data["o"] = origin;
            
            body["l"] = log_data;
            
            // Build the publish command
            // V2 API requires Body as a JSON object (NOT stringified)
            nlohmann::json command_obj;
            command_obj["Target"] = target;
            command_obj["Body"] = body;  // Body as object, not string
            
            // Build the full message
            // The server expects BOTH Targets and PublishCommands in every message
            // For PUBLISH: Targets contains the channel, PublishCommands has the command data
            nlohmann::json message;
            message["Operation"] = "PUBLISH";
            message["PublishCommands"] = nlohmann::json::array({command_obj});
            
            std::string msg = message.dump();
            Logger::Debug("Sending PiShock WebSocket PUBLISH: " + msg);
            
            return ws_client_->SendText(msg);
            
        } catch (const std::exception& e) {
            SetError("Failed to build publish command: " + std::string(e.what()));
            return false;
        }
    }

    std::string PiShockWebSocketManager::GetChannelTarget() const {
        // For direct operations in V2, use the ops channel format: c{clientId}-ops
        // For share code operations, use: c{clientId}-sops-{sharecode}
        return "c" + config_->pishock_client_id + "-ops";
    }

    // Multi-device methods
    void PiShockWebSocketManager::SendBeepMulti(int duration, const std::string& reason, const std::string& device_serial) {
        if (!ValidateCredentials()) {
            SetError("Invalid PiShock credentials");
            return;
        }

        if (!IsConnected()) {
            SetError("Not connected to WebSocket");
            return;
        }

        if (!CheckRateLimit()) {
            SetError("Rate limit exceeded");
            return;
        }

        try {
            // Determine which shocker devices to use
            std::vector<int> shocker_ids_to_use;
            std::vector<int> device_indices;
            
            if (device_serial.empty()) {
                // No specific device - use ALL configured shocker devices
                for (int i = 0; i < 5; ++i) {
                    if (config_->pishock_shocker_ids[i] != 0) {
                        shocker_ids_to_use.push_back(config_->pishock_shocker_ids[i]);
                        device_indices.push_back(i);
                    }
                }
            } else {
                // Look up which shock devices are enabled for this device
                auto shock_it = config_->device_shock_ids.find(device_serial);
                if (shock_it != config_->device_shock_ids.end()) {
                    for (int i = 0; i < 5; ++i) {
                        if (shock_it->second[i] && config_->pishock_shocker_ids[i] != 0) {
                            shocker_ids_to_use.push_back(config_->pishock_shocker_ids[i]);
                            device_indices.push_back(i);
                        }
                    }
                }
                
                // If no specific shock devices are configured for this device, use ALL configured devices as fallback
                if (shocker_ids_to_use.empty()) {
                    for (int i = 0; i < 5; ++i) {
                        if (config_->pishock_shocker_ids[i] != 0) {
                            shocker_ids_to_use.push_back(config_->pishock_shocker_ids[i]);
                            device_indices.push_back(i);
                        }
                    }
                }
            }
            
            if (shocker_ids_to_use.empty()) {
                SetError("No shocker devices configured");
                return;
            }

            Logger::Info("Sending PiShock Beep to " + std::to_string(shocker_ids_to_use.size()) + " device(s)");

            // Send command to all selected devices using multiple entries in a single message
            int duration_ms = (std::max)(1, (std::min)(15, duration)) * 1000;
            bool success = SendPublishCommandMulti(shocker_ids_to_use, "b", 0, duration_ms, "StayPutVR");
            
            if (action_callback_) {
                action_callback_("Beep", success, success ? "Action sent successfully" : "Failed to send");
            }

        } catch (const std::exception& e) {
            std::string error = "PiShock multi beep action failed: " + std::string(e.what());
            SetError(error);
        }
    }

    void PiShockWebSocketManager::SendVibrateMulti(int duration, const std::string& reason, const std::string& device_serial) {
        if (!ValidateCredentials()) {
            SetError("Invalid PiShock credentials");
            return;
        }

        if (!IsConnected()) {
            SetError("Not connected to WebSocket");
            return;
        }

        if (!CheckRateLimit()) {
            SetError("Rate limit exceeded");
            return;
        }

        try {
            // Determine which shocker devices to use
            std::vector<int> shocker_ids_to_use;
            std::vector<int> device_indices;
            
            if (device_serial.empty()) {
                // No specific device - use ALL configured shocker devices
                for (int i = 0; i < 5; ++i) {
                    if (config_->pishock_shocker_ids[i] != 0) {
                        shocker_ids_to_use.push_back(config_->pishock_shocker_ids[i]);
                        device_indices.push_back(i);
                    }
                }
            } else {
                // Look up which shock devices are enabled for this device
                auto shock_it = config_->device_shock_ids.find(device_serial);
                if (shock_it != config_->device_shock_ids.end()) {
                    for (int i = 0; i < 5; ++i) {
                        if (shock_it->second[i] && config_->pishock_shocker_ids[i] != 0) {
                            shocker_ids_to_use.push_back(config_->pishock_shocker_ids[i]);
                            device_indices.push_back(i);
                        }
                    }
                }
                
                // If no specific shock devices are configured for this device, use ALL configured devices as fallback
                if (shocker_ids_to_use.empty()) {
                    for (int i = 0; i < 5; ++i) {
                        if (config_->pishock_shocker_ids[i] != 0) {
                            shocker_ids_to_use.push_back(config_->pishock_shocker_ids[i]);
                            device_indices.push_back(i);
                        }
                    }
                }
            }
            
            if (shocker_ids_to_use.empty()) {
                SetError("No shocker devices configured");
                return;
            }

            // Send individual commands to each device with their specific intensity
            std::vector<nlohmann::json> commands;
            for (size_t i = 0; i < shocker_ids_to_use.size(); ++i) {
                int device_index = device_indices[i];
                float intensity_normalized;
                
                // Use individual disobedience intensities if enabled, otherwise use master
                if (config_->pishock_use_individual_disobedience_intensities) {
                    intensity_normalized = config_->pishock_individual_disobedience_intensities[device_index];
                } else {
                    intensity_normalized = config_->pishock_disobedience_intensity;
                }
                
                int intensity = (std::max)(1, ConvertIntensityToAPI(intensity_normalized));
                
                Logger::Info("Sending PiShock Vibrate to device " + std::to_string(device_index) + 
                           " (ID: " + std::to_string(shocker_ids_to_use[i]) + ")" +
                           " (Intensity: " + std::to_string(intensity) + 
                           ", Duration: " + std::to_string(duration) + "ms)");

                // Build command for this device
                nlohmann::json body;
                body["id"] = shocker_ids_to_use[i];
                body["m"] = "v";  // vibrate
                body["i"] = intensity;
                body["d"] = duration;
                body["r"] = true;
                
                nlohmann::json log_data;
                log_data["u"] = config_->pishock_user_id;
                log_data["ty"] = "api";
                log_data["w"] = false;
                log_data["h"] = false;
                log_data["o"] = "StayPutVR";
                
                body["l"] = log_data;
                
                nlohmann::json command_obj;
                command_obj["Target"] = GetChannelTarget();
                command_obj["Body"] = body;
                
                commands.push_back(command_obj);
            }

            // Send all commands in a single message
            nlohmann::json message;
            message["Operation"] = "PUBLISH";
            message["PublishCommands"] = commands;
            
            std::string msg = message.dump();
            Logger::Debug("Sending PiShock multi-device PUBLISH: " + msg);
            
            bool success = ws_client_->SendText(msg);
            
            if (action_callback_) {
                action_callback_("Vibrate", success, success ? "Action sent successfully" : "Failed to send");
            }

        } catch (const std::exception& e) {
            std::string error = "PiShock multi vibrate action failed: " + std::string(e.what());
            SetError(error);
        }
    }

    void PiShockWebSocketManager::SendShockMulti(int duration, const std::string& reason, const std::string& device_serial) {
        if (!ValidateCredentials()) {
            SetError("Invalid PiShock credentials");
            return;
        }

        if (!IsConnected()) {
            SetError("Not connected to WebSocket");
            return;
        }

        if (!CheckShockCooldown()) {
            std::string cooldown_msg = "Shock cooldown active (waiting " + 
                                      std::to_string((int)config_->shock_cooldown_seconds) + "s between shocks)";
            Logger::Info(cooldown_msg);
            SetError(cooldown_msg);
            if (action_callback_) {
                action_callback_("Shock", false, cooldown_msg);
            }
            return;
        }

        if (!CheckRateLimit()) {
            SetError("Rate limit exceeded");
            return;
        }

        try {
            // Determine which shocker devices to use
            std::vector<int> shocker_ids_to_use;
            std::vector<int> device_indices;
            
            if (device_serial.empty()) {
                // No specific device - use ALL configured shocker devices
                for (int i = 0; i < 5; ++i) {
                    if (config_->pishock_shocker_ids[i] != 0) {
                        shocker_ids_to_use.push_back(config_->pishock_shocker_ids[i]);
                        device_indices.push_back(i);
                    }
                }
            } else {
                // Look up which shock devices are enabled for this device
                auto shock_it = config_->device_shock_ids.find(device_serial);
                if (shock_it != config_->device_shock_ids.end()) {
                    for (int i = 0; i < 5; ++i) {
                        if (shock_it->second[i] && config_->pishock_shocker_ids[i] != 0) {
                            shocker_ids_to_use.push_back(config_->pishock_shocker_ids[i]);
                            device_indices.push_back(i);
                        }
                    }
                }
                
                // If no specific shock devices are configured for this device, use ALL configured devices as fallback
                if (shocker_ids_to_use.empty()) {
                    for (int i = 0; i < 5; ++i) {
                        if (config_->pishock_shocker_ids[i] != 0) {
                            shocker_ids_to_use.push_back(config_->pishock_shocker_ids[i]);
                            device_indices.push_back(i);
                        }
                    }
                }
            }
            
            if (shocker_ids_to_use.empty()) {
                SetError("No shocker devices configured");
                return;
            }

            // Send individual commands to each device with their specific intensity
            std::vector<nlohmann::json> commands;
            for (size_t i = 0; i < shocker_ids_to_use.size(); ++i) {
                int device_index = device_indices[i];
                float intensity_normalized;
                
                // Use individual disobedience intensities if enabled, otherwise use master
                if (config_->pishock_use_individual_disobedience_intensities) {
                    intensity_normalized = config_->pishock_individual_disobedience_intensities[device_index];
                } else {
                    intensity_normalized = config_->pishock_disobedience_intensity;
                }
                
                int intensity = (std::max)(1, ConvertIntensityToAPI(intensity_normalized));
                
                Logger::Info("Sending PiShock Shock to device " + std::to_string(device_index) + 
                           " (ID: " + std::to_string(shocker_ids_to_use[i]) + ")" +
                           " (Intensity: " + std::to_string(intensity) + 
                           ", Duration: " + std::to_string(duration) + "ms)");

                // Build command for this device
                nlohmann::json body;
                body["id"] = shocker_ids_to_use[i];
                body["m"] = "s";  // shock
                body["i"] = intensity;
                body["d"] = duration;
                body["r"] = true;
                
                nlohmann::json log_data;
                log_data["u"] = config_->pishock_user_id;
                log_data["ty"] = "api";
                log_data["w"] = false;
                log_data["h"] = false;
                log_data["o"] = "StayPutVR";
                
                body["l"] = log_data;
                
                nlohmann::json command_obj;
                command_obj["Target"] = GetChannelTarget();
                command_obj["Body"] = body;
                
                commands.push_back(command_obj);
            }

            // Send all commands in a single message
            nlohmann::json message;
            message["Operation"] = "PUBLISH";
            message["PublishCommands"] = commands;
            
            std::string msg = message.dump();
            Logger::Debug("Sending PiShock multi-device PUBLISH: " + msg);
            
            bool success = ws_client_->SendText(msg);
            
            if (action_callback_) {
                action_callback_("Shock", success, success ? "Action sent successfully" : "Failed to send");
            }

        } catch (const std::exception& e) {
            std::string error = "PiShock multi shock action failed: " + std::string(e.what());
            SetError(error);
        }
    }

    bool PiShockWebSocketManager::SendPublishCommandMulti(
        const std::vector<int>& shocker_ids,
        const std::string& mode, 
        int intensity, 
        int duration, 
        const std::string& origin) {
        
        if (!ws_client_ || !connected_) {
            SetError("WebSocket not connected");
            return false;
        }

        try {
            std::vector<nlohmann::json> commands;
            
            for (int shocker_id : shocker_ids) {
                // Build the command body
                nlohmann::json body;
                body["id"] = shocker_id;
                body["m"] = mode;
                body["i"] = intensity;
                body["d"] = duration;
                body["r"] = true;
                
                // Log metadata
                nlohmann::json log_data;
                log_data["u"] = config_->pishock_user_id;
                log_data["ty"] = "api";
                log_data["w"] = false;
                log_data["h"] = false;
                log_data["o"] = origin;
                
                body["l"] = log_data;
                
                // Build the publish command
                nlohmann::json command_obj;
                command_obj["Target"] = GetChannelTarget();
                command_obj["Body"] = body;
                
                commands.push_back(command_obj);
            }
            
            // Build the full message
            nlohmann::json message;
            message["Operation"] = "PUBLISH";
            message["PublishCommands"] = commands;
            
            std::string msg = message.dump();
            Logger::Debug("Sending PiShock WebSocket multi PUBLISH: " + msg);
            
            return ws_client_->SendText(msg);
            
        } catch (const std::exception& e) {
            SetError("Failed to build publish command: " + std::string(e.what()));
            return false;
        }
    }

} // namespace StayPutVR

