#include "PiShockManager.hpp"
#include <thread>
#include <sstream>
#include <algorithm>

namespace StayPutVR {

    PiShockManager::PiShockManager()
        : config_(nullptr)
        , enabled_(false)
        , user_agreement_(false)
        , last_action_time_(std::chrono::steady_clock::now())
        , last_shock_time_(std::chrono::steady_clock::now())
        , last_error_("")
        , action_callback_(nullptr)
    {
    }

    PiShockManager::~PiShockManager() {
        Shutdown();
    }

    bool PiShockManager::Initialize(Config* config) {
        if (!config) {
            SetError("Invalid configuration provided");
            return false;
        }

        config_ = config;
        enabled_ = config_->pishock_enabled;
        user_agreement_ = config_->pishock_user_agreement;

        Logger::Info("PiShockManager initialized");
        return true;
    }

    void PiShockManager::Shutdown() {
        enabled_ = false;
        user_agreement_ = false;
        config_ = nullptr;
        Logger::Info("PiShockManager shutdown");
    }

    void PiShockManager::Update() {
        // Update any periodic tasks if needed
        // Currently no periodic tasks required
    }

    bool PiShockManager::ValidateConfiguration() const {
        if (!config_) return false;
        
        return !config_->pishock_username.empty() &&
               !config_->pishock_api_key.empty() &&
               !config_->pishock_share_code.empty();
    }

    bool PiShockManager::IsFullyConfigured() const {
        return ValidateConfiguration() && 
               config_->pishock_enabled && 
               config_->pishock_user_agreement;
    }

    void PiShockManager::TriggerDisobedienceActions(const std::string& device_serial) {
        if (!IsEnabled()) {
            Logger::Info("PiShock not enabled, skipping disobedience actions");
            return;
        }

        if (!CanTriggerAction()) {
            Logger::Info("Rate limit active, skipping disobedience actions");
            return;
        }

        Logger::Info("Triggering PiShock disobedience actions for device: " + 
                   (device_serial.empty() ? "ALL" : device_serial));

        // Execute configured disobedience actions
        if (config_->pishock_disobedience_beep) {
            SendBeep(0, 1, "Disobedience - Beep");
        }

        if (config_->pishock_disobedience_vibrate) {
            int intensity = ConvertIntensityToAPI(config_->pishock_disobedience_intensity);
            int duration = ConvertDurationToAPI(config_->pishock_disobedience_duration);
            SendVibrate(intensity, duration, "Disobedience - Vibrate");
        }

        if (config_->pishock_disobedience_shock) {
            int intensity = ConvertIntensityToAPI(config_->pishock_disobedience_intensity);
            int duration = ConvertDurationToAPI(config_->pishock_disobedience_duration);
            SendShock(intensity, duration, "Disobedience - Shock");
        }
    }

    void PiShockManager::TriggerWarningActions(const std::string& device_serial) {
        if (!IsEnabled()) {
            Logger::Info("PiShock not enabled, skipping warning actions");
            return;
        }

        if (!CanTriggerAction()) {
            Logger::Info("Rate limit active, skipping warning actions");
            return;
        }

        Logger::Info("Triggering PiShock warning actions for device: " + 
                   (device_serial.empty() ? "ALL" : device_serial));

        // Execute warning actions (typically lighter than disobedience)
        SendBeep(0, 1, "Warning - Beep");
        
        if (config_->pishock_disobedience_vibrate) {
            int intensity = (std::max)(1, ConvertIntensityToAPI(config_->pishock_disobedience_intensity) / 2);
            SendVibrate(intensity, 1, "Warning - Vibrate");
        }
    }

    void PiShockManager::TestActions() {
        if (!IsEnabled()) {
            SetError("PiShock not enabled");
            return;
        }

        if (!ValidateConfiguration()) {
            SetError("PiShock configuration invalid");
            return;
        }

        Logger::Info("Testing configured PiShock out-of-bounds actions...");
        
        // Test the actual configured disobedience actions
        TriggerDisobedienceActions("TEST");
    }

    void PiShockManager::SendBeep(int intensity, int duration, const std::string& reason) {
        PiShockActionData action;
        action.type = PiShockActionType::BEEP;
        action.intensity = 0; // Beeps don't use intensity
        action.duration = (std::max)(1, (std::min)(15, duration));
        action.reason = reason;

        ExecuteActionAsync(action);
    }

    void PiShockManager::SendVibrate(int intensity, int duration, const std::string& reason) {
        if (!ValidateActionParameters(intensity, duration)) {
            SetError("Invalid vibrate parameters");
            return;
        }

        PiShockActionData action;
        action.type = PiShockActionType::VIBRATE;
        action.intensity = intensity;
        action.duration = duration;
        action.reason = reason;

        ExecuteActionAsync(action);
    }

    void PiShockManager::SendShock(int intensity, int duration, const std::string& reason) {
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

        PiShockActionData action;
        action.type = PiShockActionType::SHOCK;
        action.intensity = intensity;
        action.duration = duration;
        action.reason = reason;

        ExecuteActionAsync(action);
    }

    std::string PiShockManager::GetConnectionStatus() const {
        if (!config_) return "Not initialized";
        if (!config_->pishock_enabled) return "Disabled";
        if (!config_->pishock_user_agreement) return "User agreement required";
        if (!ValidateConfiguration()) return "Configuration incomplete";
        return "Ready";
    }

    bool PiShockManager::CanTriggerAction() const {
        std::lock_guard<std::mutex> lock(rate_limit_mutex_);
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_action_time_);
        return elapsed.count() >= RATE_LIMIT_SECONDS;
    }

    int PiShockManager::ConvertIntensityToAPI(float normalized_intensity) {
        return (std::max)(1, (std::min)(100, static_cast<int>(normalized_intensity * 100.0f)));
    }

    int PiShockManager::ConvertDurationToAPI(float duration_seconds) {
        return (std::max)(1, (std::min)(15, static_cast<int>(std::round(duration_seconds))));
    }

    void PiShockManager::SetError(const std::string& error) {
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_ = error;
        Logger::Error("PiShockManager Error: " + error);
    }

    bool PiShockManager::CheckRateLimit() {
        std::lock_guard<std::mutex> lock(rate_limit_mutex_);
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_action_time_);
        
        if (elapsed.count() >= RATE_LIMIT_SECONDS) {
            last_action_time_ = now;
            return true;
        }
        return false;
    }

    void PiShockManager::UpdateRateLimit() {
        std::lock_guard<std::mutex> lock(rate_limit_mutex_);
        last_action_time_ = std::chrono::steady_clock::now();
    }

    bool PiShockManager::CheckShockCooldown() {
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

    void PiShockManager::UpdateShockCooldown() {
        std::lock_guard<std::mutex> lock(shock_cooldown_mutex_);
        last_shock_time_ = std::chrono::steady_clock::now();
    }

    void PiShockManager::ExecuteAction(const PiShockActionData& action) {
        if (!ValidateCredentials()) {
            SetError("Invalid PiShock credentials");
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
            // Prepare PiShock API request
            std::string url = "https://do.pishock.com/api/apioperate/";
            
            std::stringstream post_data;
            post_data << "Username=" << config_->pishock_username
                     << "&Name=StayPutVR"
                     << "&Code=" << config_->pishock_share_code
                     << "&Intensity=" << action.intensity
                     << "&Duration=" << action.duration
                     << "&Apikey=" << config_->pishock_api_key
                     << "&Op=" << static_cast<int>(action.type);

            Logger::Info("Sending PiShock " + ActionTypeToString(action.type) + 
                       " (Intensity: " + std::to_string(action.intensity) + 
                       ", Duration: " + std::to_string(action.duration) + 
                       ", Reason: " + action.reason + ")");

            std::string response;
            bool success = SendPiShockCommand(
                config_->pishock_username,
                config_->pishock_api_key,
                config_->pishock_share_code,
                static_cast<int>(action.type),
                action.intensity,
                action.duration,
                response
            );

            // success is already set by SendPiShockCommand
            
            LogAction(action, success, response);
            
            if (action_callback_) {
                action_callback_(ActionTypeToString(action.type), success, 
                               success ? "Action completed successfully" : response);
            }

        } catch (const std::exception& e) {
            std::string error = "PiShock action failed: " + std::string(e.what());
            SetError(error);
            LogAction(action, false, error);
            
            if (action_callback_) {
                action_callback_(ActionTypeToString(action.type), false, error);
            }
        }
    }

    void PiShockManager::ExecuteActionAsync(const PiShockActionData& action) {
        // Execute in separate thread to avoid blocking UI
        std::thread([this, action]() {
            ExecuteAction(action);
        }).detach();
    }

    bool PiShockManager::ValidateCredentials() const {
        return config_ && 
               !config_->pishock_username.empty() &&
               !config_->pishock_api_key.empty() &&
               !config_->pishock_share_code.empty();
    }

    bool PiShockManager::ValidateActionParameters(int intensity, int duration) const {
        return intensity >= 1 && intensity <= 100 &&
               duration >= 1 && duration <= 15;
    }

    void PiShockManager::LogAction(const PiShockActionData& action, bool success, const std::string& response) const {
        std::stringstream log_msg;
        log_msg << "PiShock " << ActionTypeToString(action.type) 
                << " (I:" << action.intensity << ", D:" << action.duration << ") "
                << (success ? "SUCCESS" : "FAILED");
        
        if (!action.reason.empty()) {
            log_msg << " - " << action.reason;
        }
        
        if (!success && !response.empty()) {
            log_msg << " - " << response;
        }
        
        Logger::Info(log_msg.str());
    }

    std::string PiShockManager::ActionTypeToString(PiShockActionType type) const {
        switch (type) {
            case PiShockActionType::BEEP: return "Beep";
            case PiShockActionType::VIBRATE: return "Vibrate";
            case PiShockActionType::SHOCK: return "Shock";
            default: return "Unknown";
        }
    }

} // namespace StayPutVR 