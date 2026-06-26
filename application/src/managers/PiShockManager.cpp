#include "PiShockManager.hpp"
#include <sstream>
#include <algorithm>
#include <cmath>

namespace StayPutVR {

    PiShockManager::PiShockManager()
        : ShockDeviceBase(/*rate_limit_seconds=*/2)
    {
    }

    bool PiShockManager::OnInitialize() {
        Logger::Info("PiShockManager initialized");
        return true;
    }

    void PiShockManager::OnShutdown() {
        Logger::Info("PiShockManager shutdown");
    }

    bool PiShockManager::CheckEnabled() const {
        if (!config_) return false;
        auto cfg_lock = config_->ReadLock();
        return config_->pishock_enabled && config_->pishock_user_agreement;
    }

    bool PiShockManager::IsEnabled() const {
        return CheckEnabled();
    }

    bool PiShockManager::ValidateConfiguration() const {
        if (!config_) return false;
        auto cfg_lock = config_->ReadLock();
        return !config_->pishock_username.empty() &&
               !config_->pishock_api_key.empty() &&
               !config_->pishock_share_code.empty();
    }

    bool PiShockManager::IsFullyConfigured() const {
        return ValidateConfiguration() && IsEnabled();
    }

    void PiShockManager::TriggerDisobedienceActions(const std::string& device_serial) {
        if (!IsEnabled()) {
            Logger::Info("PiShock not enabled, skipping disobedience actions");
            return;
        }

        // Rate-limit once per event so a single disobedience event's
        // beep + vibrate + shock all fire (CheckRateLimit consumes the budget;
        // the per-action check in ExecuteAction was dropping later actions).
        if (!CheckRateLimit()) {
            Logger::Info("Rate limit active, skipping disobedience actions");
            return;
        }

        Logger::Info("Triggering PiShock disobedience actions for device: " +
                   (device_serial.empty() ? "ALL" : device_serial));

        // Snapshot config under read lock
        bool do_beep, do_vibrate, do_shock;
        float intensity, duration;
        {
            auto cfg_lock = config_->ReadLock();
            do_beep = config_->pishock_disobedience_beep;
            do_vibrate = config_->pishock_disobedience_vibrate;
            do_shock = config_->pishock_disobedience_shock;
            intensity = config_->pishock_disobedience_intensity;
            duration = config_->pishock_disobedience_duration;
        }

        if (do_beep) {
            SendBeep(0, 1, "Disobedience - Beep");
        }

        if (do_vibrate) {
            SendVibrate(ConvertIntensityToAPI(intensity),
                       ConvertDurationToAPI(duration),
                       "Disobedience - Vibrate");
        }

        if (do_shock) {
            SendShock(ConvertIntensityToAPI(intensity),
                     ConvertDurationToAPI(duration),
                     "Disobedience - Shock");
        }
    }

    void PiShockManager::TriggerWarningActions(const std::string& device_serial) {
        if (!IsEnabled()) {
            Logger::Info("PiShock not enabled, skipping warning actions");
            return;
        }

        // Rate-limit warnings on their OWN timer so a stream of warnings never
        // consumes the disobedience/shock budget (see CheckWarningRateLimit).
        if (!CheckWarningRateLimit()) {
            Logger::Info("Warning rate limit active, skipping warning actions");
            return;
        }

        Logger::Info("Triggering PiShock warning actions for device: " +
                   (device_serial.empty() ? "ALL" : device_serial));

        // Snapshot config under read lock. Mirror TriggerDisobedienceActions but
        // read the warning-zone fields, and gate every action on its own flag --
        // previously this hardcoded an unconditional beep and read the
        // *disobedience* vibrate/intensity, so warnings fired beep+vibrate even
        // when nothing warning-related was configured.
        bool do_beep, do_vibrate, do_shock;
        float intensity, duration;
        {
            auto cfg_lock = config_->ReadLock();
            do_beep = config_->pishock_warning_beep;
            do_vibrate = config_->pishock_warning_vibrate;
            do_shock = config_->pishock_warning_shock;
            intensity = config_->pishock_warning_intensity;
            duration = config_->pishock_warning_duration;
        }

        if (do_beep) {
            SendBeep(0, 1, "Warning - Beep");
        }

        if (do_vibrate) {
            SendVibrate(ConvertIntensityToAPI(intensity),
                       ConvertDurationToAPI(duration),
                       "Warning - Vibrate");
        }

        if (do_shock) {
            SendShock(ConvertIntensityToAPI(intensity),
                     ConvertDurationToAPI(duration),
                     "Warning - Shock");
        }
    }

    void PiShockManager::TriggerShock(float intensity, float duration_seconds, const std::string& reason) {
        if (!IsEnabled()) {
            Logger::Info("PiShock not enabled, skipping external shock");
            return;
        }
        if (!CheckRateLimit()) {
            Logger::Info("Rate limit active, skipping external shock");
            return;
        }
        SendShock(ConvertIntensityToAPI(intensity), ConvertDurationToAPI(duration_seconds), reason);
    }

    void PiShockManager::TriggerShockIndividual(float duration_seconds, const std::string& reason) {
        if (!IsEnabled()) {
            Logger::Info("PiShock not enabled, skipping external shock");
            return;
        }
        // Legacy API is single-device, so "individual" simply uses the configured
        // disobedience intensity rather than a flat bite/shock intensity.
        float intensity;
        {
            auto cfg_lock = config_->ReadLock();
            intensity = config_->pishock_disobedience_intensity;
        }
        TriggerShock(intensity, duration_seconds, reason);
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
        TriggerDisobedienceActions("TEST");
    }

    void PiShockManager::SendBeep(int intensity, int duration, const std::string& reason) {
        PiShockActionData action;
        action.type = PiShockActionType::BEEP;
        action.intensity = 0;
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
            float cooldown_secs;
            {
                auto cfg_lock = config_->ReadLock();
                cooldown_secs = config_->shock_cooldown_seconds;
            }
            std::string cooldown_msg = "Shock cooldown active (waiting " +
                                      std::to_string((int)cooldown_secs) + "s between shocks)";
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
        auto cfg_lock = config_->ReadLock();
        if (!config_->pishock_enabled) return "Disabled";
        if (!config_->pishock_user_agreement) return "User agreement required";
        if (!ValidateConfiguration()) return "Configuration incomplete";
        return "Ready";
    }

    int PiShockManager::ConvertIntensityToAPI(float normalized_intensity) {
        return (std::max)(1, (std::min)(100, static_cast<int>(normalized_intensity * 100.0f)));
    }

    int PiShockManager::ConvertDurationToAPI(float duration_seconds) {
        return (std::max)(1, (std::min)(15, static_cast<int>(std::round(duration_seconds))));
    }

    void PiShockManager::ExecuteAction(const PiShockActionData& action) {
        if (!ValidateCredentials()) {
            SetError("Invalid PiShock credentials");
            if (action_callback_) {
                action_callback_(ActionTypeToString(action.type), false, "Invalid credentials");
            }
            return;
        }

        // Rate limiting is applied once per event in TriggerDisobedienceActions/
        // TriggerWarningActions, so every action in one event fires. The shock
        // cooldown still gates shocks individually (see SendShock()).

        try {
            std::string username, api_key, share_code;
            {
                auto cfg_lock = config_->ReadLock();
                username = config_->pishock_username;
                api_key = config_->pishock_api_key;
                share_code = config_->pishock_share_code;
            }

            Logger::Info("Sending PiShock " + ActionTypeToString(action.type) +
                       " (Intensity: " + std::to_string(action.intensity) +
                       ", Duration: " + std::to_string(action.duration) +
                       ", Reason: " + action.reason + ")");

            std::string response;
            bool success = SendPiShockCommand(
                username, api_key, share_code,
                static_cast<int>(action.type),
                action.intensity, action.duration,
                response
            );

            LogAction(action, success, response);
            RecordCommandResult(success);

            if (action_callback_) {
                action_callback_(ActionTypeToString(action.type), success,
                               success ? "Action completed successfully" : response);
            }

        } catch (const std::exception& e) {
            std::string error = "PiShock action failed: " + std::string(e.what());
            SetError(error);
            RecordCommandResult(false);
            LogAction(action, false, error);
            if (action_callback_) {
                action_callback_(ActionTypeToString(action.type), false, error);
            }
        }
    }

    void PiShockManager::ExecuteActionAsync(const PiShockActionData& action) {
        EnqueueWork([this, action]() {
            ExecuteAction(action);
        });
    }

    bool PiShockManager::ValidateCredentials() const {
        if (!config_) return false;
        auto cfg_lock = config_->ReadLock();
        return !config_->pishock_username.empty() &&
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
