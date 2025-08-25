#include "OpenShockManager.hpp"
#include <thread>
#include <sstream>
#include <algorithm>

namespace StayPutVR {

    OpenShockManager::OpenShockManager()
        : config_(nullptr)
        , enabled_(false)
        , user_agreement_(false)
        , last_action_time_(std::chrono::steady_clock::now())
        , last_error_("")
        , action_callback_(nullptr)
    {
    }

    OpenShockManager::~OpenShockManager() {
        Shutdown();
    }

    bool OpenShockManager::Initialize(Config* config) {
        if (!config) {
            SetError("Invalid configuration provided");
            return false;
        }

        config_ = config;
        enabled_ = config_->openshock_enabled;
        user_agreement_ = config_->openshock_user_agreement;

        Logger::Info("OpenShockManager initialized");
        return true;
    }

    void OpenShockManager::Shutdown() {
        enabled_ = false;
        user_agreement_ = false;
        config_ = nullptr;
        Logger::Info("OpenShockManager shutdown");
    }

    void OpenShockManager::Update() {
        // Update any periodic tasks if needed
        // Currently no periodic tasks required
    }

    bool OpenShockManager::ValidateConfiguration() const {
        if (!config_) return false;
        
        return !config_->openshock_api_token.empty() &&
               !config_->openshock_device_id.empty() &&
               !config_->openshock_server_url.empty();
    }

    bool OpenShockManager::IsFullyConfigured() const {
        return ValidateConfiguration() && 
               config_->openshock_enabled && 
               config_->openshock_user_agreement;
    }

    void OpenShockManager::TriggerDisobedienceActions(const std::string& device_serial) {
        if (!IsEnabled()) {
            Logger::Info("OpenShock not enabled, skipping disobedience actions");
            return;
        }

        if (!CanTriggerAction()) {
            Logger::Info("Rate limit active, skipping disobedience actions");
            return;
        }

        Logger::Info("Triggering OpenShock disobedience actions for device: " + 
                   (device_serial.empty() ? "ALL" : device_serial));

        // Execute configured disobedience actions
        switch (config_->openshock_disobedience_action) {
            case 1: // Shock
                {
                    int intensity = ConvertIntensityToAPI(config_->openshock_disobedience_intensity);
                    int duration = ConvertDurationToAPI(config_->openshock_disobedience_duration);
                    SendShock(intensity, duration, "Disobedience - Shock");
                }
                break;
            case 2: // Vibrate
                {
                    int intensity = ConvertIntensityToAPI(config_->openshock_disobedience_intensity);
                    int duration = ConvertDurationToAPI(config_->openshock_disobedience_duration);
                    SendVibrate(intensity, duration, "Disobedience - Vibrate");
                }
                break;
            default: // 0 = None
                break;
        }
    }

    void OpenShockManager::TriggerWarningActions(const std::string& device_serial) {
        if (!IsEnabled()) {
            Logger::Info("OpenShock not enabled, skipping warning actions");
            return;
        }

        if (!CanTriggerAction()) {
            Logger::Info("Rate limit active, skipping warning actions");
            return;
        }

        Logger::Info("Triggering OpenShock warning actions for device: " + 
                   (device_serial.empty() ? "ALL" : device_serial));

        // Execute warning actions (typically lighter than disobedience)
        switch (config_->openshock_warning_action) {
            case 1: // Shock
                {
                    int intensity = (std::max)(1, ConvertIntensityToAPI(config_->openshock_warning_intensity) / 2);
                    SendShock(intensity, 1000, "Warning - Shock");
                }
                break;
            case 2: // Vibrate
                {
                    int intensity = (std::max)(1, ConvertIntensityToAPI(config_->openshock_warning_intensity) / 2);
                    SendVibrate(intensity, 1000, "Warning - Vibrate");
                }
                break;
            default: // 0 = None
                break;
        }
    }

    void OpenShockManager::TestActions() {
        if (!IsEnabled()) {
            SetError("OpenShock not enabled");
            return;
        }

        if (!ValidateConfiguration()) {
            SetError("OpenShock configuration invalid");
            return;
        }

        Logger::Info("Testing configured OpenShock out-of-bounds actions...");
        
        // Test the actual configured disobedience actions
        TriggerDisobedienceActions("TEST");
    }

    void OpenShockManager::SendSound(int intensity, int duration, const std::string& reason) {
        OpenShockActionData action;
        action.type = OpenShockActionType::SOUND;
        action.intensity = 0; // Sound doesn't use intensity
        action.duration = (std::max)(300, (std::min)(65535, duration)); // API minimum is 300ms, max 65535ms
        action.reason = reason;

        ExecuteActionAsync(action);
    }

    void OpenShockManager::SendVibrate(int intensity, int duration, const std::string& reason) {
        if (!ValidateActionParameters(intensity, duration)) {
            SetError("Invalid vibrate parameters");
            return;
        }

        OpenShockActionData action;
        action.type = OpenShockActionType::VIBRATE;
        action.intensity = intensity;
        action.duration = duration;
        action.reason = reason;

        ExecuteActionAsync(action);
    }

    void OpenShockManager::SendShock(int intensity, int duration, const std::string& reason) {
        if (!ValidateActionParameters(intensity, duration)) {
            SetError("Invalid shock parameters");
            return;
        }

        OpenShockActionData action;
        action.type = OpenShockActionType::SHOCK;
        action.intensity = intensity;
        action.duration = duration;
        action.reason = reason;

        ExecuteActionAsync(action);
    }

    std::string OpenShockManager::GetConnectionStatus() const {
        if (!config_) return "Not initialized";
        if (!config_->openshock_enabled) return "Disabled";
        if (!config_->openshock_user_agreement) return "User agreement required";
        if (!ValidateConfiguration()) return "Configuration incomplete";
        return "Ready";
    }

    bool OpenShockManager::CanTriggerAction() const {
        std::lock_guard<std::mutex> lock(rate_limit_mutex_);
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_action_time_);
        return elapsed.count() >= RATE_LIMIT_SECONDS;
    }

    int OpenShockManager::ConvertIntensityToAPI(float normalized_intensity) {
        return (std::max)(1, (std::min)(100, static_cast<int>(normalized_intensity * 100.0f)));
    }

    int OpenShockManager::ConvertDurationToAPI(float normalized_duration) {
        // OpenShock uses milliseconds, convert from normalized 0.0-1.0 to 300-11014ms
        // API minimum is 300ms, maximum is 65535ms
        // Formula: 300 + (normalized_duration * 10714) so 0.07 gives ~1050ms
        return (std::max)(300, (std::min)(65535, static_cast<int>(300 + (normalized_duration * 10714.0f))));
    }

    void OpenShockManager::SetError(const std::string& error) {
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_ = error;
        Logger::Error("OpenShockManager Error: " + error);
    }

    bool OpenShockManager::CheckRateLimit() {
        std::lock_guard<std::mutex> lock(rate_limit_mutex_);
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_action_time_);
        
        if (elapsed.count() >= RATE_LIMIT_SECONDS) {
            last_action_time_ = now;
            return true;
        }
        return false;
    }

    void OpenShockManager::UpdateRateLimit() {
        std::lock_guard<std::mutex> lock(rate_limit_mutex_);
        last_action_time_ = std::chrono::steady_clock::now();
    }

    void OpenShockManager::ExecuteAction(const OpenShockActionData& action) {
        if (!ValidateCredentials()) {
            SetError("Invalid OpenShock credentials");
            if (action_callback_) {
                action_callback_(ActionTypeToString(action.type), false, "Invalid credentials");
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
            Logger::Info("Sending OpenShock " + ActionTypeToString(action.type) + 
                       " (Intensity: " + std::to_string(action.intensity) + 
                       ", Duration: " + std::to_string(action.duration) + "ms" +
                       ", Reason: " + action.reason + ")");

            std::string response;
            bool success = SendOpenShockCommand(
                config_->openshock_server_url,
                config_->openshock_api_token,
                config_->openshock_device_id,
                static_cast<int>(action.type),
                action.intensity,
                action.duration,
                response
            );
            
            LogAction(action, success, response);
            
            if (action_callback_) {
                action_callback_(ActionTypeToString(action.type), success, 
                               success ? "Action completed successfully" : response);
            }

        } catch (const std::exception& e) {
            std::string error = "OpenShock action failed: " + std::string(e.what());
            SetError(error);
            LogAction(action, false, error);
            
            if (action_callback_) {
                action_callback_(ActionTypeToString(action.type), false, error);
            }
        }
    }

    void OpenShockManager::ExecuteActionAsync(const OpenShockActionData& action) {
        // Execute in separate thread to avoid blocking UI
        std::thread([this, action]() {
            ExecuteAction(action);
        }).detach();
    }

    bool OpenShockManager::ValidateCredentials() const {
        return config_ && 
               !config_->openshock_api_token.empty() &&
               !config_->openshock_device_id.empty() &&
               !config_->openshock_server_url.empty();
    }

    bool OpenShockManager::ValidateActionParameters(int intensity, int duration) const {
        return intensity >= 1 && intensity <= 100 &&
               duration >= 300 && duration <= 65535; // OpenShock uses milliseconds, min 300ms, max 65535ms
    }

    void OpenShockManager::LogAction(const OpenShockActionData& action, bool success, const std::string& response) const {
        std::stringstream log_msg;
        log_msg << "OpenShock " << ActionTypeToString(action.type) 
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

    std::string OpenShockManager::ActionTypeToString(OpenShockActionType type) const {
        switch (type) {
            case OpenShockActionType::SOUND: return "Sound";
            case OpenShockActionType::VIBRATE: return "Vibrate";
            case OpenShockActionType::SHOCK: return "Shock";
            default: return "Unknown";
        }
    }

} // namespace StayPutVR
