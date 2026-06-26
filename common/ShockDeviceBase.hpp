#pragma once

#include "IShockDeviceManager.hpp"
#include "Config.hpp"
#include "Logger.hpp"
#include "AsyncWorkQueue.hpp"
#include "LinkStatus.hpp"

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

    // Connection-status snapshot for the Status tab. Stateless HTTP managers
    // have no live link, so this reflects configuration plus the outcome of the
    // most recent network command (the only reachability signal available).
    LinkStatus GetLinkStatus() const;

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
    // Separate rate limit for warning-zone actions. Warnings must throttle among
    // themselves WITHOUT consuming the main (disobedience/shock) budget -- otherwise
    // repeatedly entering the warning zone keeps bumping last_action_time_ and the
    // disobedience shock that follows is never allowed through.
    bool CheckWarningRateLimit();
    bool CheckShockCooldown();
    void UpdateShockCooldown();

    // Enqueue work on the bounded async worker thread.
    bool EnqueueWork(std::function<void()> work);

    // Record the outcome of a network command (the actual HTTP send), so the
    // Status tab can show whether the device is currently reachable. Validation
    // / cooldown / rate-limit rejections are NOT command results and must not be
    // reported here.
    void RecordCommandResult(bool success);

    Config* config_ = nullptr;
    ShockActionCallback action_callback_;

private:
    std::atomic<bool> enabled_{false};
    AsyncWorkQueue work_queue_;

    // Rate limiting
    int rate_limit_seconds_;
    mutable std::chrono::steady_clock::time_point last_action_time_;
    mutable std::chrono::steady_clock::time_point last_warning_time_;
    mutable std::mutex rate_limit_mutex_;

    // Shock cooldown
    mutable std::chrono::steady_clock::time_point last_shock_time_;
    mutable std::mutex shock_cooldown_mutex_;

    // Error handling
    std::string last_error_;
    mutable std::mutex error_mutex_;

    // Last network-command outcome (for GetLinkStatus).
    mutable std::mutex command_mutex_;
    std::chrono::steady_clock::time_point last_command_time_{};
    bool had_command_ = false;
    bool last_command_ok_ = false;
};

} // namespace StayPutVR
