#include "ShockDeviceBase.hpp"

namespace StayPutVR {

ShockDeviceBase::ShockDeviceBase(int rate_limit_seconds)
    : rate_limit_seconds_(rate_limit_seconds)
    , last_action_time_(std::chrono::steady_clock::now())
    , last_shock_time_(std::chrono::steady_clock::now())
{
}

ShockDeviceBase::~ShockDeviceBase() {
    Shutdown();
}

bool ShockDeviceBase::Initialize(Config* config) {
    if (!config) {
        SetError("Invalid configuration provided");
        return false;
    }
    config_ = config;
    if (!OnInitialize()) return false;
    work_queue_.Start();
    return true;
}

void ShockDeviceBase::Shutdown() {
    OnShutdown();
    work_queue_.Shutdown();
    config_ = nullptr;
}

void ShockDeviceBase::Update() {
    // Base has no periodic work; subclasses override if needed.
}

std::string ShockDeviceBase::GetLastError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

bool ShockDeviceBase::CanTriggerAction() const {
    std::lock_guard<std::mutex> lock(rate_limit_mutex_);
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_action_time_);
    return elapsed.count() >= rate_limit_seconds_;
}

void ShockDeviceBase::SetActionCallback(ShockActionCallback callback) {
    action_callback_ = std::move(callback);
}

// --- Protected utilities ---

void ShockDeviceBase::SetError(const std::string& error) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = error;
    Logger::Error(error);
}

bool ShockDeviceBase::CheckRateLimit() {
    std::lock_guard<std::mutex> lock(rate_limit_mutex_);
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_action_time_);
    if (elapsed.count() >= rate_limit_seconds_) {
        last_action_time_ = now;
        return true;
    }
    return false;
}

void ShockDeviceBase::UpdateRateLimit() {
    std::lock_guard<std::mutex> lock(rate_limit_mutex_);
    last_action_time_ = std::chrono::steady_clock::now();
}

bool ShockDeviceBase::CheckShockCooldown() {
    if (!config_) return true;

    bool cooldown_enabled;
    float cooldown_seconds;
    {
        auto cfg_lock = config_->ReadLock();
        cooldown_enabled = config_->shock_cooldown_enabled;
        cooldown_seconds = config_->shock_cooldown_seconds;
    }

    if (!cooldown_enabled) return true;

    std::lock_guard<std::mutex> lock(shock_cooldown_mutex_);
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_shock_time_);

    if (elapsed.count() >= cooldown_seconds) {
        last_shock_time_ = now;
        return true;
    }
    return false;
}

void ShockDeviceBase::UpdateShockCooldown() {
    std::lock_guard<std::mutex> lock(shock_cooldown_mutex_);
    last_shock_time_ = std::chrono::steady_clock::now();
}

bool ShockDeviceBase::EnqueueWork(std::function<void()> work) {
    return work_queue_.Enqueue(std::move(work));
}

void ShockDeviceBase::RecordCommandResult(bool success) {
    std::lock_guard<std::mutex> lock(command_mutex_);
    had_command_ = true;
    last_command_ok_ = success;
    last_command_time_ = std::chrono::steady_clock::now();
}

LinkStatus ShockDeviceBase::GetLinkStatus() const {
    LinkStatus s;

    // GetConnectionStatus() (virtual) reports configuration readiness:
    // "Disabled" / "User agreement required" / "Configuration incomplete" / "Ready".
    const std::string cs = GetConnectionStatus();
    if (cs != "Ready") {
        s.state = LinkState::Disabled;
        s.detail = cs;
        return s;
    }

    std::lock_guard<std::mutex> lock(command_mutex_);
    if (!had_command_) {
        s.state = LinkState::Connected;
        s.detail = "configured (HTTP); no commands sent yet";
        return s;
    }

    auto secs = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - last_command_time_).count();
    if (last_command_ok_) {
        s.state = LinkState::Connected;
        s.detail = "last command OK (" + std::to_string(secs) + "s ago)";
        s.last_ok = last_command_time_;
    } else {
        s.state = LinkState::Failed;
        s.detail = "last command FAILED (" + std::to_string(secs) + "s ago)";
        s.last_error = GetLastError();
    }
    return s;
}

} // namespace StayPutVR
