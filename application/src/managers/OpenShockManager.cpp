#include "OpenShockManager.hpp"
#include <sstream>
#include <algorithm>
#include <array>
#include <unordered_map>
#include <vector>

namespace StayPutVR {

    OpenShockManager::OpenShockManager()
        : ShockDeviceBase(/*rate_limit_seconds=*/1)
    {
    }

    bool OpenShockManager::OnInitialize() {
        Logger::Info("OpenShockManager initialized");
        return true;
    }

    void OpenShockManager::OnShutdown() {
        Logger::Info("OpenShockManager shutdown");
    }

    bool OpenShockManager::CheckEnabled() const {
        if (!config_) return false;
        auto cfg_lock = config_->ReadLock();
        return config_->openshock_enabled && config_->openshock_user_agreement;
    }

    bool OpenShockManager::IsEnabled() const {
        return CheckEnabled();
    }

    bool OpenShockManager::ValidateConfiguration() const {
        return ValidateCredentials();
    }

    bool OpenShockManager::IsFullyConfigured() const {
        return ValidateConfiguration() && IsEnabled();
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

        int disob_action;
        float disob_duration;
        {
            auto cfg_lock = config_->ReadLock();
            disob_action = config_->openshock_disobedience_action;
            disob_duration = config_->openshock_disobedience_duration;
        }

        switch (disob_action) {
            case 1: // Shock
                {
                    int duration = ConvertDurationToAPI(disob_duration);
                    SendShockWithIndividualIntensities(duration, "Disobedience - Shock", device_serial, true);
                }
                break;
            case 2: // Vibrate
                {
                    int duration = ConvertDurationToAPI(disob_duration);
                    SendVibrateWithIndividualIntensities(duration, "Disobedience - Vibrate", device_serial, true);
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

        int warn_action;
        {
            auto cfg_lock = config_->ReadLock();
            warn_action = config_->openshock_warning_action;
        }

        switch (warn_action) {
            case 1: // Shock
                SendShockWithIndividualIntensities(1000, "Warning - Shock", device_serial, false);
                break;
            case 2: // Vibrate
                SendVibrateWithIndividualIntensities(1000, "Warning - Vibrate", device_serial, false);
                break;
            default: // 0 = None
                break;
        }
    }

    void OpenShockManager::TriggerShock(float intensity, float duration_seconds, const std::string& reason) {
        if (!IsEnabled()) {
            Logger::Info("OpenShock not enabled, skipping external shock");
            return;
        }
        // SendShock -> ExecuteAction already applies the rate limit and shock
        // cooldown, so don't double-gate here. Duration is API ms (300..65535).
        int duration_ms = (std::max)(300, (std::min)(65535, static_cast<int>(duration_seconds * 1000.0f)));
        SendShock(ConvertIntensityToAPI(intensity), duration_ms, reason, "");
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
        TriggerDisobedienceActions("TEST");
    }

    void OpenShockManager::SendShockWithIndividualIntensities(int duration, const std::string& reason, const std::string& device_serial, bool is_disobedience) {
        if (!ValidateCredentials()) {
            SetError("Invalid OpenShock credentials");
            return;
        }

        if (!CheckRateLimit()) {
            SetError("Rate limit exceeded");
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

        try {
            std::string server_url, api_token;
            std::array<std::string, 5> device_ids;
            std::unordered_map<std::string, std::array<bool, 5>> device_shock_map;
            bool use_individual_disob, use_individual_warn;
            std::array<float, 5> individual_disob_intensities, individual_warn_intensities;
            float master_disob_intensity, master_warn_intensity;
            {
                auto cfg_lock = config_->ReadLock();
                server_url = config_->openshock_server_url;
                api_token = config_->openshock_api_token;
                device_ids = config_->openshock_device_ids;
                device_shock_map = config_->device_shock_ids;
                use_individual_disob = config_->openshock_use_individual_disobedience_intensities;
                use_individual_warn = config_->openshock_use_individual_warning_intensities;
                individual_disob_intensities = config_->openshock_individual_disobedience_intensities;
                individual_warn_intensities = config_->openshock_individual_warning_intensities;
                master_disob_intensity = config_->openshock_master_disobedience_intensity;
                master_warn_intensity = config_->openshock_master_warning_intensity;
            }

            std::vector<std::string> device_ids_to_use;
            std::vector<int> device_indices;

            if (device_serial.empty()) {
                if (!device_ids[0].empty()) {
                    device_ids_to_use.push_back(device_ids[0]);
                    device_indices.push_back(0);
                }
            } else {
                auto shock_it = device_shock_map.find(device_serial);
                if (shock_it != device_shock_map.end()) {
                    for (int i = 0; i < 5; ++i) {
                        if (shock_it->second[i] && !device_ids[i].empty()) {
                            device_ids_to_use.push_back(device_ids[i]);
                            device_indices.push_back(i);
                        }
                    }
                }

                if (device_ids_to_use.empty() && !device_ids[0].empty()) {
                    device_ids_to_use.push_back(device_ids[0]);
                    device_indices.push_back(0);
                }
            }

            if (device_ids_to_use.empty()) {
                SetError("No shock devices configured");
                return;
            }

            for (size_t i = 0; i < device_ids_to_use.size(); ++i) {
                int device_index = device_indices[i];
                float intensity_normalized;

                if (is_disobedience) {
                    if (use_individual_disob) {
                        intensity_normalized = individual_disob_intensities[device_index];
                    } else {
                        intensity_normalized = master_disob_intensity;
                    }
                } else {
                    if (use_individual_warn) {
                        intensity_normalized = individual_warn_intensities[device_index] / 2.0f;
                    } else {
                        intensity_normalized = master_warn_intensity / 2.0f;
                    }
                }

                int intensity = (std::max)(1, ConvertIntensityToAPI(intensity_normalized));

                Logger::Info("Sending OpenShock Shock to device " + std::to_string(device_index) +
                           " (ID: " + device_ids_to_use[i] + ")" +
                           " (Intensity: " + std::to_string(intensity) +
                           ", Duration: " + std::to_string(duration) + "ms" +
                           ", Reason: " + reason + ")");

                std::string response;
                bool success = SendOpenShockCommand(
                    server_url, api_token,
                    device_ids_to_use[i],
                    0, // 0 = Shock
                    intensity, duration, response
                );

                if (!success) {
                    Logger::Error("Failed to send shock to device " + std::to_string(device_index) + ": " + response);
                }
            }

        } catch (const std::exception& e) {
            std::string error = "OpenShock individual shock action failed: " + std::string(e.what());
            SetError(error);
            Logger::Error(error);
        }
    }

    void OpenShockManager::SendVibrateWithIndividualIntensities(int duration, const std::string& reason, const std::string& device_serial, bool is_disobedience) {
        if (!ValidateCredentials()) {
            SetError("Invalid OpenShock credentials");
            return;
        }

        if (!CheckRateLimit()) {
            SetError("Rate limit exceeded");
            return;
        }

        try {
            std::string server_url, api_token;
            std::array<std::string, 5> device_ids;
            std::unordered_map<std::string, std::array<bool, 5>> device_shock_map;
            bool use_individual_disob, use_individual_warn;
            std::array<float, 5> individual_disob_intensities, individual_warn_intensities;
            float master_disob_intensity, master_warn_intensity;
            {
                auto cfg_lock = config_->ReadLock();
                server_url = config_->openshock_server_url;
                api_token = config_->openshock_api_token;
                device_ids = config_->openshock_device_ids;
                device_shock_map = config_->device_shock_ids;
                use_individual_disob = config_->openshock_use_individual_disobedience_intensities;
                use_individual_warn = config_->openshock_use_individual_warning_intensities;
                individual_disob_intensities = config_->openshock_individual_disobedience_intensities;
                individual_warn_intensities = config_->openshock_individual_warning_intensities;
                master_disob_intensity = config_->openshock_master_disobedience_intensity;
                master_warn_intensity = config_->openshock_master_warning_intensity;
            }

            std::vector<std::string> device_ids_to_use;
            std::vector<int> device_indices;

            if (device_serial.empty()) {
                if (!device_ids[0].empty()) {
                    device_ids_to_use.push_back(device_ids[0]);
                    device_indices.push_back(0);
                }
            } else {
                auto shock_it = device_shock_map.find(device_serial);
                if (shock_it != device_shock_map.end()) {
                    for (int i = 0; i < 5; ++i) {
                        if (shock_it->second[i] && !device_ids[i].empty()) {
                            device_ids_to_use.push_back(device_ids[i]);
                            device_indices.push_back(i);
                        }
                    }
                }

                if (device_ids_to_use.empty() && !device_ids[0].empty()) {
                    device_ids_to_use.push_back(device_ids[0]);
                    device_indices.push_back(0);
                }
            }

            if (device_ids_to_use.empty()) {
                SetError("No shock devices configured");
                return;
            }

            for (size_t i = 0; i < device_ids_to_use.size(); ++i) {
                int device_index = device_indices[i];
                float intensity_normalized;

                if (is_disobedience) {
                    if (use_individual_disob) {
                        intensity_normalized = individual_disob_intensities[device_index];
                    } else {
                        intensity_normalized = master_disob_intensity;
                    }
                } else {
                    if (use_individual_warn) {
                        intensity_normalized = individual_warn_intensities[device_index] / 2.0f;
                    } else {
                        intensity_normalized = master_warn_intensity / 2.0f;
                    }
                }

                int intensity = (std::max)(1, ConvertIntensityToAPI(intensity_normalized));

                Logger::Info("Sending OpenShock Vibrate to device " + std::to_string(device_index) +
                           " (ID: " + device_ids_to_use[i] + ")" +
                           " (Intensity: " + std::to_string(intensity) +
                           ", Duration: " + std::to_string(duration) + "ms" +
                           ", Reason: " + reason + ")");

                std::string response;
                bool success = SendOpenShockCommand(
                    server_url, api_token,
                    device_ids_to_use[i],
                    1, // 1 = Vibrate
                    intensity, duration, response
                );

                if (!success) {
                    Logger::Error("Failed to send vibrate to device " + std::to_string(device_index) + ": " + response);
                }
            }

        } catch (const std::exception& e) {
            std::string error = "OpenShock individual vibrate action failed: " + std::string(e.what());
            SetError(error);
            Logger::Error(error);
        }
    }

    void OpenShockManager::SendSound(int intensity, int duration, const std::string& reason, const std::string& device_serial) {
        OpenShockActionData action;
        action.type = OpenShockActionType::SOUND;
        action.intensity = 0;
        action.duration = (std::max)(300, (std::min)(65535, duration));
        action.reason = reason;
        ExecuteActionAsyncMulti(action, device_serial);
    }

    void OpenShockManager::SendVibrate(int intensity, int duration, const std::string& reason, const std::string& device_serial) {
        if (!ValidateActionParameters(intensity, duration)) {
            SetError("Invalid vibrate parameters");
            return;
        }

        OpenShockActionData action;
        action.type = OpenShockActionType::VIBRATE;
        action.intensity = intensity;
        action.duration = duration;
        action.reason = reason;
        ExecuteActionAsyncMulti(action, device_serial);
    }

    void OpenShockManager::SendShock(int intensity, int duration, const std::string& reason, const std::string& device_serial) {
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

        OpenShockActionData action;
        action.type = OpenShockActionType::SHOCK;
        action.intensity = intensity;
        action.duration = duration;
        action.reason = reason;
        ExecuteActionAsyncMulti(action, device_serial);
    }

    std::string OpenShockManager::GetConnectionStatus() const {
        if (!config_) return "Not initialized";
        auto cfg_lock = config_->ReadLock();
        if (!config_->openshock_enabled) return "Disabled";
        if (!config_->openshock_user_agreement) return "User agreement required";
        if (!ValidateConfiguration()) return "Configuration incomplete";
        return "Ready";
    }

    int OpenShockManager::ConvertIntensityToAPI(float normalized_intensity) {
        return (std::max)(1, (std::min)(100, static_cast<int>(normalized_intensity * 100.0f)));
    }

    int OpenShockManager::ConvertDurationToAPI(float normalized_duration) {
        return (std::max)(300, (std::min)(65535, static_cast<int>(300 + (normalized_duration * 10714.0f))));
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
            std::string server_url, api_token;
            std::string device_id_0;
            {
                auto cfg_lock = config_->ReadLock();
                server_url = config_->openshock_server_url;
                api_token = config_->openshock_api_token;
                device_id_0 = config_->openshock_device_ids[0];
            }

            Logger::Info("Sending OpenShock " + ActionTypeToString(action.type) +
                       " (Intensity: " + std::to_string(action.intensity) +
                       ", Duration: " + std::to_string(action.duration) + "ms" +
                       ", Reason: " + action.reason + ")");

            std::vector<std::string> device_ids;
            if (!device_id_0.empty()) {
                device_ids.push_back(device_id_0);
            }

            std::string response;
            bool success = false;
            if (!device_ids.empty()) {
                success = SendOpenShockCommandMulti(
                    server_url, api_token, device_ids,
                    static_cast<int>(action.type),
                    action.intensity, action.duration, response
                );
            } else {
                SetError("No shock devices configured");
                response = "No shock devices configured";
            }

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
        EnqueueWork([this, action]() {
            ExecuteAction(action);
        });
    }

    void OpenShockManager::ExecuteActionAsyncMulti(const OpenShockActionData& action, const std::string& device_serial) {
        EnqueueWork([this, action, device_serial]() {
            ExecuteActionMulti(action, device_serial);
        });
    }

    void OpenShockManager::ExecuteActionMulti(const OpenShockActionData& action, const std::string& device_serial) {
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
            std::string server_url, api_token;
            std::array<std::string, 5> device_ids;
            std::unordered_map<std::string, std::array<bool, 5>> device_shock_map;
            {
                auto cfg_lock = config_->ReadLock();
                server_url = config_->openshock_server_url;
                api_token = config_->openshock_api_token;
                device_ids = config_->openshock_device_ids;
                device_shock_map = config_->device_shock_ids;
            }

            std::vector<std::string> device_ids_to_use;

            if (device_serial.empty()) {
                if (!device_ids[0].empty()) {
                    device_ids_to_use.push_back(device_ids[0]);
                }
            } else {
                auto shock_it = device_shock_map.find(device_serial);
                if (shock_it != device_shock_map.end()) {
                    for (int i = 0; i < 5; ++i) {
                        if (shock_it->second[i] && !device_ids[i].empty()) {
                            device_ids_to_use.push_back(device_ids[i]);
                        }
                    }
                }

                if (device_ids_to_use.empty() && !device_ids[0].empty()) {
                    device_ids_to_use.push_back(device_ids[0]);
                }
            }

            if (device_ids_to_use.empty()) {
                SetError("No shock devices configured");
                if (action_callback_) {
                    action_callback_(ActionTypeToString(action.type), false, "No shock devices configured");
                }
                return;
            }

            Logger::Info("Sending OpenShock " + ActionTypeToString(action.type) +
                       " to " + std::to_string(device_ids_to_use.size()) + " device(s)" +
                       " (Intensity: " + std::to_string(action.intensity) +
                       ", Duration: " + std::to_string(action.duration) + "ms" +
                       ", Reason: " + action.reason + ")");

            std::string response;
            bool success = SendOpenShockCommandMulti(
                server_url, api_token, device_ids_to_use,
                static_cast<int>(action.type),
                action.intensity, action.duration, response
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

    bool OpenShockManager::ValidateCredentials() const {
        if (!config_) return false;

        auto cfg_lock = config_->ReadLock();
        bool has_device_id = false;
        for (const auto& id : config_->openshock_device_ids) {
            if (!id.empty()) {
                has_device_id = true;
                break;
            }
        }

        return !config_->openshock_api_token.empty() &&
               has_device_id &&
               !config_->openshock_server_url.empty();
    }

    bool OpenShockManager::ValidateActionParameters(int intensity, int duration) const {
        return intensity >= 1 && intensity <= 100 &&
               duration >= 300 && duration <= 65535;
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
