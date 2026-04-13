#pragma once

#include "IShockDeviceManager.hpp"
#include "Config.hpp"
#include "Logger.hpp"
#include "AsyncWorkQueue.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

namespace StayPutVR {

// Abstract base implementing the boilerplate shared by all shock device
// managers: rate limiting, shock cooldown, error tracking, async work
// queue, and config lifetime management.
//
// Subclasses implement the pure-virtual hooks and add device-specific
// logic (credentials, API calls, multi-device routing, etc.).
class ShockDeviceBase : public IShockDeviceManager {
public:
    explicit ShockDeviceBase(int rate_limit_seconds = 2);
    ~ShockDeviceBase() override;

    // IShockDeviceManager lifecycle
    bool Initialize(Config* config) override;
    void Shutdown() override;
    void Update() override;

    // Shared implementations
    std::string GetLastError() const override;
    bool CanTriggerAction() const override;
    void SetActionCallback(ShockActionCallback callback) override;

protected:
    // --- Hooks for subclasses ---

    // Called during Initialize after base setup. Return false to fail init.
    virtual bool OnInitialize() { return true; }

    // Called during Shutdown before base teardown.
    virtual void OnShutdown() {}

    // Subclass must return whether it considers itself enabled.
    virtual bool CheckEnabled() const = 0;

    // --- Utilities available to subclasses ---

    void SetError(const std::string& error);
    bool CheckRateLimit();
    void UpdateRateLimit();
    bool CheckShockCooldown();
    void UpdateShockCooldown();

    // Enqueue work on the bounded async worker thread.
    bool EnqueueWork(std::function<void()> work);

    Config* config_ = nullptr;
    ShockActionCallback action_callback_;

private:
    std::atomic<bool> enabled_{false};
    AsyncWorkQueue work_queue_;

    // Rate limiting
    int rate_limit_seconds_;
    mutable std::chrono::steady_clock::time_point last_action_time_;
    mutable std::mutex rate_limit_mutex_;

    // Shock cooldown
    mutable std::chrono::steady_clock::time_point last_shock_time_;
    mutable std::mutex shock_cooldown_mutex_;

    // Error handling
    std::string last_error_;
    mutable std::mutex error_mutex_;
};

} // namespace StayPutVR
