#pragma once

#include <string>
#include <functional>

namespace StayPutVR {

class Config;

// Callback type shared by all shock device managers.
using ShockActionCallback = std::function<void(const std::string& action_type, bool success, const std::string& message)>;

// Interface for shock/haptic device managers (PiShock, OpenShock,
// PiShockWebSocket, Buttplug). Defines the lifecycle contract that
// UIManager depends on.
class IShockDeviceManager {
public:
    virtual ~IShockDeviceManager() = default;

    // Lifecycle — called from the UI thread.
    virtual bool Initialize(Config* config) = 0;
    virtual void Shutdown() = 0;
    virtual void Update() = 0;

    // Configuration
    virtual bool ValidateConfiguration() const = 0;
    virtual bool IsEnabled() const = 0;

    // Action triggering
    virtual void TriggerDisobedienceActions(const std::string& device_serial = "") = 0;
    virtual void TriggerWarningActions(const std::string& device_serial = "") = 0;
    virtual void TestActions() = 0;

    // Status / error
    virtual std::string GetConnectionStatus() const = 0;
    virtual std::string GetLastError() const = 0;
    virtual bool CanTriggerAction() const = 0;

    // Callback
    virtual void SetActionCallback(ShockActionCallback callback) = 0;
};

} // namespace StayPutVR
