#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <optional>
#include <unordered_map>
#include <variant>
#include <functional>

namespace StayPutVR {

// OSCQuery (mDNS) server. Advertises StayPutVR's OSC receive port via
// `_osc._udp` and an OSCQuery HTTP tree via `_oscjson._tcp`, and browses the
// network for VRChat's `_osc._udp` service so sends can be retargeted to the
// port VRChat is actually listening on. Ported from yip_companion.
//
// All socket code is behind #ifdef _WIN32 so the Windows build is unaffected;
// the Linux development build reuses the same POSIX paths.
class OSCQueryServer {
public:
    enum Access { NoAccess = 0, ReadOnly = 1, WriteOnly = 2, ReadWrite = 3 };

    struct ParamNode {
        std::string full_path;
        std::string osc_type;
        Access access = NoAccess;
        std::variant<float, int, bool, std::string> value;
    };

    OSCQueryServer();
    ~OSCQueryServer();

    bool Start(int osc_udp_port);
    void Stop();
    bool IsRunning() const { return running_; }

    // True once the mDNS listen socket is bound and StayPutVR is advertising
    // its service. If false while running (e.g. UDP 5353 is held by avahi or
    // another OSC app), VRChat cannot discover StayPutVR's port — surface this.
    bool IsAdvertising() const { return mdns_advertising_; }

    void AddParameter(const std::string& path, const std::string& osc_type,
                      Access access, std::variant<float, int, bool, std::string> initial_value = 0.0f);
    void UpdateValue(const std::string& path, std::variant<float, int, bool, std::string> value);

    std::optional<int> GetVRChatOSCPort() const;
    std::optional<int> GetVRChatQueryPort() const;
    bool IsVRChatConnected() const;

    int GetHTTPPort() const { return http_port_; }
    int GetOSCPort() const { return osc_port_; }

    // Fired (from the browse thread) the first time VRChat's OSC port is
    // discovered, and again if it changes. Used to retarget OSCManager's send
    // socket. The callback must be cheap and thread-safe.
    void SetVRChatPortDiscoveredCallback(std::function<void(int)> cb);

private:
    void HTTPThread();
    void MDNSBrowseThread();
    void MDNSListenThread();

    std::string BuildHostInfo() const;
    std::string BuildFullTree() const;
    std::string BuildNodeJSON(const std::string& path) const;

    std::thread http_thread_;
    std::atomic<bool> running_{false};
    int http_port_ = 0;
    int osc_port_ = 0;
    void* http_server_ = nullptr;

    std::thread mdns_browse_thread_;
    std::thread mdns_listen_thread_;
    std::mutex mdns_sockets_mutex_;
    std::vector<int> mdns_sockets_; // one mDNS listen socket per local interface
    std::atomic<bool> mdns_advertising_{false};

    mutable std::mutex vrc_mutex_;
    std::optional<int> vrc_osc_port_;
    std::optional<int> vrc_query_port_;

    mutable std::mutex param_mutex_;
    std::vector<ParamNode> params_;
    std::unordered_map<std::string, size_t> param_index_;

    mutable std::mutex callback_mutex_;
    std::function<void(int)> vrc_port_discovered_callback_;

    std::string service_name_ = "StayPutVR";
    std::string hostname_;
};

} // namespace StayPutVR
