#include "DGLabManager.hpp"

#include "../../../common/Logger.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include "../../../common/WinsockCompat.hpp"
#endif

#include <algorithm>
#include <cstdio>
#include <random>
#include <sstream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace StayPutVR {

namespace {

    // LAN address the QR code should point the phone at. UDP "connect" to a
    // public address selects the outbound interface without sending a packet.
    std::string DetectLocalIp() {
        std::string result;
#ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return result;
#endif
        SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
        if (s != INVALID_SOCKET) {
            sockaddr_in remote{};
            remote.sin_family = AF_INET;
            remote.sin_port = htons(53);
            inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);
            if (connect(s, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) == 0) {
                sockaddr_in local{};
#ifdef _WIN32
                int len = sizeof(local);
#else
                socklen_t len = sizeof(local);
#endif
                if (getsockname(s, reinterpret_cast<sockaddr*>(&local), &len) == 0) {
                    char buf[INET_ADDRSTRLEN] = {0};
                    if (inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf)))
                        result = buf;
                }
            }
            closesocket(s);
        }
#ifdef _WIN32
        WSACleanup();
#endif
        return result;
    }

    // One waveform entry = 100ms: 4 frequency bytes + 4 strength bytes, hex.
    std::string MakeWaveEntry(int frequency, int s0, int s1, int s2, int s3) {
        char buf[17];
        std::snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X%02X%02X",
                      frequency, frequency, frequency, frequency, s0, s1, s2, s3);
        return std::string(buf);
    }

} // namespace

DGLabManager::~DGLabManager() {
    Shutdown();
}

bool DGLabManager::Initialize(Config* config) {
    if (!config) return false;
    config_ = config;

    if (config_->dglab_client_id.empty()) {
        config_->dglab_client_id = GenerateUuid();
    }

    server_ = std::make_unique<DGLabServer>();
    last_heartbeat_ = std::chrono::steady_clock::now();
    last_pulse_ = std::chrono::steady_clock::now() - std::chrono::seconds(60);
    last_warning_ = last_pulse_;
    last_server_attempt_ = std::chrono::steady_clock::now() - std::chrono::seconds(60);

    if (Logger::IsInitialized())
        Logger::Info("DGLabManager: initialized");
    return true;
}

void DGLabManager::Shutdown() {
    if (server_ && server_->IsRunning()) {
        // Best effort: leave the device quiet if a phone is still bound.
        if (link_state_ == LinkState::Bound) StopAll();
        server_->Stop();
    }
    server_.reset();
    link_state_ = LinkState::ServerStopped;
    config_ = nullptr;
}

void DGLabManager::Update() {
    if (!config_ || !server_) return;

    EnsureServerState();

    for (const auto& event : server_->DrainEvents()) {
        HandleEvent(event);
    }

    if (link_state_ == LinkState::Bound) {
        auto now = std::chrono::steady_clock::now();
        if (now - last_heartbeat_ >= std::chrono::seconds(60)) {
            last_heartbeat_ = now;
            SendJson("heartbeat", "200");
        }
    }
}

void DGLabManager::EnsureServerState() {
    const bool should_run = IsEnabled();
    const bool is_running = server_->IsRunning();

    if (should_run && !is_running) {
        auto now = std::chrono::steady_clock::now();
        if (now - last_server_attempt_ >= std::chrono::seconds(5)) {
            last_server_attempt_ = now;
            if (server_->Start(config_->dglab_server_port)) {
                link_state_ = LinkState::WaitingForScan;
            } else {
                SetError(server_->GetLastError());
            }
        }
    } else if (!should_run && is_running) {
        if (link_state_ == LinkState::Bound) StopAll();
        server_->Stop();
        link_state_ = LinkState::ServerStopped;
    }
}

void DGLabManager::HandleEvent(const DGLabServer::Event& event) {
    switch (event.type) {
        case DGLabServer::EventType::ClientConnected: {
            // Assign the phone its connection id and start the bind handshake:
            // the app replies with a bind request carrying our clientId (from
            // the QR) plus this targetId.
            const std::string assigned_id = GenerateUuid();
            {
                std::lock_guard<std::mutex> lk(target_id_mutex_);
                target_id_ = assigned_id;
            }
            link_state_ = LinkState::Binding;
            json msg = {
                {"type", "bind"},
                {"clientId", assigned_id},
                {"targetId", ""},
                {"message", "targetId"}
            };
            server_->SendText(msg.dump());
            if (Logger::IsInitialized())
                Logger::Info("DGLabManager: phone connected, awaiting bind");
            break;
        }
        case DGLabServer::EventType::ClientDisconnected:
            if (link_state_ != LinkState::ServerStopped)
                link_state_ = server_->IsRunning() ? LinkState::WaitingForScan
                                                  : LinkState::ServerStopped;
            strength_[0] = strength_[1] = 0;
            if (Logger::IsInitialized())
                Logger::Info("DGLabManager: phone disconnected");
            break;
        case DGLabServer::EventType::TextMessage:
            HandleAppMessage(event.payload);
            break;
    }
}

void DGLabManager::HandleAppMessage(const std::string& text) {
    json j;
    try {
        j = json::parse(text);
    } catch (const std::exception&) {
        if (Logger::IsInitialized())
            Logger::Warning("DGLabManager: unparseable message from app: " + text);
        return;
    }

    const std::string type = j.value("type", "");
    const std::string message = j.value("message", "");

    if (type == "bind") {
        const std::string client_id = j.value("clientId", "");
        const std::string bind_target = j.value("targetId", "");
        json reply = {
            {"type", "bind"},
            {"clientId", client_id},
            {"targetId", bind_target}
        };
        std::string expected_target;
        {
            std::lock_guard<std::mutex> lk(target_id_mutex_);
            expected_target = target_id_;
        }
        if (client_id == config_->dglab_client_id && bind_target == expected_target) {
            reply["message"] = "200";
            server_->SendText(reply.dump());
            link_state_ = LinkState::Bound;
            last_heartbeat_ = std::chrono::steady_clock::now();
            if (Logger::IsInitialized())
                Logger::Info("DGLabManager: bind complete, device ready");
            // Pin channel strength at the configured limits; output stays
            // silent until a pulse waveform is sent.
            ApplyStrengthLimits();
        } else {
            reply["message"] = "400";
            server_->SendText(reply.dump());
            SetError("Bind rejected: QR code mismatch (re-scan the QR code)");
        }
    } else if (type == "msg") {
        if (message.rfind("strength-", 0) == 0) {
            // strength-A+B+limitA+limitB, all ints.
            int a = 0, b = 0, la = 200, lb = 200;
            if (std::sscanf(message.c_str(), "strength-%d+%d+%d+%d", &a, &b, &la, &lb) == 4) {
                strength_[0] = a;
                strength_[1] = b;
                app_limit_[0] = la;
                app_limit_[1] = lb;
                // Keep strength pinned at the effective limit. Leave 0 alone:
                // 0 means the user hit the device's physical stop buttons (or
                // we zeroed it), and the next trigger re-arms it.
                for (int ch = 0; ch < 2; ++ch) {
                    if (!ChannelEnabled(ch)) continue;
                    const int limit = EffectiveLimit(ch);
                    if (strength_[ch] != 0 && strength_[ch] != limit)
                        SendStrengthSet(ch, limit);
                }
            }
        } else if (message.rfind("feedback-", 0) == 0) {
            // Feedback buttons in the phone app (0-4 = channel A, 5-9 = B).
            if (Logger::IsInitialized())
                Logger::Info("DGLabManager: app feedback button: " + message);
        }
    }
    // heartbeat echoes and anything else: nothing to do
}

void DGLabManager::SendJson(const std::string& type, const std::string& message) {
    if (!server_) return;
    std::string target;
    {
        std::lock_guard<std::mutex> lk(target_id_mutex_);
        target = target_id_;
    }
    json j = {
        {"type", type},
        {"clientId", config_ ? config_->dglab_client_id : ""},
        {"targetId", target},
        {"message", message}
    };
    server_->SendText(j.dump());
}

void DGLabManager::SendStrengthSet(int channel, int value) {
    value = std::clamp(value, 0, 200);
    // strength-<channel 1|2>+<mode 2=set absolute>+<value>
    SendJson("msg", "strength-" + std::to_string((channel & 1) + 1) + "+2+" + std::to_string(value));
}

void DGLabManager::SendPulse(int channel, float intensity, float duration_seconds) {
    intensity = std::clamp(intensity, 0.0f, 1.0f);
    duration_seconds = std::clamp(duration_seconds, 0.1f, 10.0f);

    const int entries = std::max(1, static_cast<int>(duration_seconds * 10.0f + 0.5f));
    const int frequency = std::clamp(config_->dglab_frequency, 10, 240);
    const int peak = static_cast<int>(intensity * 100.0f + 0.5f);

    json wave = json::array();
    for (int i = 0; i < entries; ++i) {
        int s = peak;
        switch (config_->dglab_waveform) {
            case 1: // Pulse: 200ms on / 200ms off
                s = ((i / 2) % 2 == 0) ? peak : 0;
                break;
            case 2: { // Ramp: climb to peak over 800ms cycles
                const int phase = i % 8;
                s = peak * (phase + 1) / 8;
                break;
            }
            default: // Steady
                break;
        }
        wave.push_back(MakeWaveEntry(frequency, s, s, s, s));
    }

    const char channel_label = (channel & 1) == 0 ? 'A' : 'B';
    SendJson("msg", std::string("pulse-") + channel_label + ":" + wave.dump());
}

std::array<bool, 2> DGLabManager::ResolveChannels(const std::string& device_serial) const {
    std::array<bool, 2> channels = {false, false};
    if (!config_) return channels;

    if (device_serial.empty()) {
        // "ALL": every globally-enabled channel.
        for (int ch = 0; ch < 2; ++ch) channels[ch] = ChannelEnabled(ch);
        return channels;
    }

    // A specific tracker fires only the channels explicitly bound to it in the
    // Devices tab. No fall-back: a tracker bound only to PiShock/OpenShock (or
    // to nothing) must not fire DG-Lab.
    auto it = config_->device_dglab_ids.find(device_serial);
    if (it == config_->device_dglab_ids.end()) return channels;
    for (int ch = 0; ch < 2; ++ch)
        channels[ch] = it->second[ch] && ChannelEnabled(ch);
    return channels;
}

bool DGLabManager::CanTriggerAction() const {
    if (!IsEnabled() || link_state_ != LinkState::Bound) return false;
    return std::chrono::steady_clock::now() - last_pulse_ >=
           std::chrono::milliseconds(RATE_LIMIT_MS);
}

void DGLabManager::FirePulse(const std::array<bool, 2>& channels, float intensity,
                             float duration_seconds, const std::string& reason) {
    if (!IsEnabled() || link_state_ != LinkState::Bound) return;
    if (!channels[0] && !channels[1]) return;

    bool fired = false;
    for (int ch = 0; ch < 2; ++ch) {
        if (!channels[ch]) continue;
        SendStrengthSet(ch, EffectiveLimit(ch));
        SendPulse(ch, intensity, duration_seconds);
        fired = true;
    }
    if (!fired) return;

    if (Logger::IsInitialized())
        Logger::Info("DGLabManager: pulse " + std::to_string(static_cast<int>(intensity * 100)) +
                     "% for " + std::to_string(duration_seconds) + "s" +
                     (reason.empty() ? "" : " (" + reason + ")"));
    if (action_callback_)
        action_callback_("pulse", true, reason);
}

void DGLabManager::TriggerShock(float intensity, float duration_seconds, const std::string& reason) {
    if (!CanTriggerAction()) return;
    last_pulse_ = std::chrono::steady_clock::now();
    FirePulse(ResolveChannels(""), intensity, duration_seconds, reason);
}

void DGLabManager::TriggerDisobedienceActions(const std::string& device_serial) {
    if (!config_ || config_->dglab_disobedience_action == 0) return;
    if (!CanTriggerAction()) return;
    last_pulse_ = std::chrono::steady_clock::now();
    FirePulse(ResolveChannels(device_serial),
              config_->dglab_disobedience_intensity,
              config_->dglab_disobedience_duration,
              device_serial.empty() ? "disobedience" : "disobedience: " + device_serial);
}

void DGLabManager::TriggerWarningActions(const std::string& device_serial) {
    if (!config_ || config_->dglab_warning_action == 0) return;
    if (!IsEnabled() || link_state_ != LinkState::Bound) return;
    auto now = std::chrono::steady_clock::now();
    if (now - last_warning_ < std::chrono::milliseconds(RATE_LIMIT_MS)) return;
    last_warning_ = now;
    FirePulse(ResolveChannels(device_serial),
              config_->dglab_warning_intensity,
              config_->dglab_warning_duration,
              device_serial.empty() ? "warning" : "warning: " + device_serial);
}

void DGLabManager::TestChannel(int channel, float intensity, float duration_seconds) {
    if (!IsEnabled() || link_state_ != LinkState::Bound) return;
    channel &= 1;
    SendStrengthSet(channel, EffectiveLimit(channel));
    SendPulse(channel, intensity, duration_seconds);
    if (action_callback_)
        action_callback_("test", true, (channel == 0) ? "channel A" : "channel B");
}

void DGLabManager::StopAll() {
    if (link_state_ != LinkState::Bound) return;
    // Flush queued waveforms, then zero both channels.
    SendJson("msg", "clear-1");
    SendJson("msg", "clear-2");
    SendStrengthSet(0, 0);
    SendStrengthSet(1, 0);
    if (Logger::IsInitialized())
        Logger::Info("DGLabManager: stop all");
}

void DGLabManager::ApplyStrengthLimits() {
    if (link_state_ != LinkState::Bound) return;
    for (int ch = 0; ch < 2; ++ch) {
        if (!ChannelEnabled(ch)) continue;
        SendStrengthSet(ch, EffectiveLimit(ch));
    }
}

int DGLabManager::EffectiveLimit(int channel) const {
    if (!config_) return 0;
    const int configured = (channel & 1) == 0 ? config_->dglab_limit_a : config_->dglab_limit_b;
    return std::clamp(std::min(configured, app_limit_[channel & 1]), 0, 200);
}

bool DGLabManager::ChannelEnabled(int channel) const {
    if (!config_) return false;
    return (channel & 1) == 0 ? config_->dglab_channel_a : config_->dglab_channel_b;
}

std::string DGLabManager::GetConnectionStatus() const {
    if (!IsEnabled()) return "Disabled";
    switch (link_state_.load()) {
        case LinkState::ServerStopped: return "Server not running";
        case LinkState::WaitingForScan: return "Waiting for QR scan";
        case LinkState::Binding: return "Phone connected, binding...";
        case LinkState::Bound:
            return "Bound (A " + std::to_string(strength_[0]) + "/" + std::to_string(app_limit_[0]) +
                   ", B " + std::to_string(strength_[1]) + "/" + std::to_string(app_limit_[1]) + ")";
    }
    return "Unknown";
}

std::string DGLabManager::GetLastError() const {
    std::lock_guard<std::mutex> lk(error_mutex_);
    return last_error_;
}

void DGLabManager::SetError(const std::string& error) {
    {
        std::lock_guard<std::mutex> lk(error_mutex_);
        last_error_ = error;
    }
    if (Logger::IsInitialized())
        Logger::Error("DGLabManager: " + error);
}

std::string DGLabManager::GetQrPayload() const {
    if (!config_ || !server_ || !server_->IsRunning()) return "";
    std::string ip = config_->dglab_server_ip;
    if (ip.empty()) {
        static std::string cached_ip;
        if (cached_ip.empty()) cached_ip = DetectLocalIp();
        ip = cached_ip.empty() ? "127.0.0.1" : cached_ip;
    }
    // Exact format required by the DG-Lab app: official download URL,
    // #DGLAB-SOCKET# tag, then our ws endpoint with the clientId as the path.
    return "https://www.dungeon-lab.com/app-download.php#DGLAB-SOCKET#ws://" +
           ip + ":" + std::to_string(config_->dglab_server_port) + "/" +
           config_->dglab_client_id;
}

std::string DGLabManager::GenerateUuid() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;
    const uint64_t hi = dist(gen);
    const uint64_t lo = dist(gen);
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%08x-%04x-4%03x-%04x-%012llx",
                  static_cast<unsigned>(hi >> 32),
                  static_cast<unsigned>((hi >> 16) & 0xFFFF),
                  static_cast<unsigned>(hi & 0x0FFF),
                  static_cast<unsigned>(0x8000 | ((lo >> 48) & 0x3FFF)),
                  static_cast<unsigned long long>(lo & 0xFFFFFFFFFFFFULL));
    return std::string(buf);
}

} // namespace StayPutVR
