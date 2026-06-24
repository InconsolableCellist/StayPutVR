#pragma once

#include <string>
#include <functional>

#include "../../../common/ShockDeviceBase.hpp"
#include "../../../common/HttpClient.hpp"

namespace StayPutVR {

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

    class OpenShockManager : public ShockDeviceBase {
    public:
        OpenShockManager();
        ~OpenShockManager() override = default;

        // IShockDeviceManager overrides
        bool ValidateConfiguration() const override;
        bool IsEnabled() const override;
        void TriggerDisobedienceActions(const std::string& device_serial = "") override;
        void TriggerWarningActions(const std::string& device_serial = "") override;
        // Fire a direct shock at an explicit intensity (0..1) and duration
        // (seconds) on all configured devices. Used by external triggers.
        void TriggerShock(float intensity, float duration_seconds, const std::string& reason = "");
        // Like TriggerShock but uses the per-device disobedience intensities
        // instead of a single supplied intensity (for OSC bite/shock).
        void TriggerShockIndividual(float duration_seconds, const std::string& reason = "");
        void TestActions() override;
        std::string GetConnectionStatus() const override;

        // OpenShock-specific action methods
        void SendSound(int intensity = 0, int duration = 1000, const std::string& reason = "", const std::string& device_serial = "");
        void SendVibrate(int intensity, int duration, const std::string& reason = "", const std::string& device_serial = "");
        void SendShock(int intensity, int duration, const std::string& reason = "", const std::string& device_serial = "");

        // Individual intensity support
        void SendShockWithIndividualIntensities(int duration, const std::string& reason, const std::string& device_serial, bool is_disobedience);
        void SendVibrateWithIndividualIntensities(int duration, const std::string& reason, const std::string& device_serial, bool is_disobedience);

        bool IsFullyConfigured() const;

        // Configuration helpers
        static int ConvertIntensityToAPI(float normalized_intensity); // 0.0-1.0 -> 1-100
        static int ConvertDurationToAPI(float normalized_duration);   // 0.0-1.0 -> 1000-15000 (ms)

    protected:
        bool OnInitialize() override;
        void OnShutdown() override;
        bool CheckEnabled() const override;

    private:
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
