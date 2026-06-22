#pragma once

#include <string>
#include <functional>

#include "../../../common/ShockDeviceBase.hpp"
#include "../../../common/HttpClient.hpp"

namespace StayPutVR {

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

    class PiShockManager : public ShockDeviceBase {
    public:
        PiShockManager();
        ~PiShockManager() override = default;

        // IShockDeviceManager overrides
        bool ValidateConfiguration() const override;
        bool IsEnabled() const override;
        void TriggerDisobedienceActions(const std::string& device_serial = "") override;
        void TriggerWarningActions(const std::string& device_serial = "") override;
        // Fire a direct shock at an explicit intensity (0..1) and duration
        // (seconds). Used by external triggers (bite / Shock param).
        void TriggerShock(float intensity, float duration_seconds, const std::string& reason = "");
        void TestActions() override;
        std::string GetConnectionStatus() const override;

        // PiShock-specific action methods
        void SendBeep(int intensity = 0, int duration = 1, const std::string& reason = "");
        void SendVibrate(int intensity, int duration, const std::string& reason = "");
        void SendShock(int intensity, int duration, const std::string& reason = "");

        bool IsFullyConfigured() const;

        // Configuration helpers
        static int ConvertIntensityToAPI(float normalized_intensity); // 0.0-1.0 -> 1-100
        static int ConvertDurationToAPI(float duration_seconds);      // 1.0-15.0 seconds -> 1-15 integer

    protected:
        bool OnInitialize() override;
        void OnShutdown() override;
        bool CheckEnabled() const override;

    private:
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
