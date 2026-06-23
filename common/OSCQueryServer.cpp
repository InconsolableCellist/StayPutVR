#include "OSCQueryServer.hpp"
#include "Logger.hpp"
#include <memory>
#include <chrono>

#ifdef _WIN32
    #include <WS2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "iphlpapi.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <ifaddrs.h>
    #include <net/if.h>
#endif

#define CPPHTTPLIB_NO_EXCEPTIONS
#include <cpp-httplib/httplib.h>

#include <mdns/mdns.h>

#include <nlohmann/json.hpp>
#include <sstream>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace StayPutVR {

using json = nlohmann::json;

// Logger helper: StayPutVR's Logger asserts/guards on initialization, so guard
// each call (matches the pattern used in OSCManager).
static inline void LogInfo(const std::string& m)    { if (Logger::IsInitialized()) Logger::Info(m); }
static inline void LogWarning(const std::string& m) { if (Logger::IsInitialized()) Logger::Warning(m); }
static inline void LogError(const std::string& m)   { if (Logger::IsInitialized()) Logger::Error(m); }
static inline void LogDebug(const std::string& m)   { if (Logger::IsInitialized()) Logger::Debug(m); }

static std::string GetLocalHostname() {
    char buf[256] = {};
#ifdef _WIN32
    DWORD size = sizeof(buf);
    GetComputerNameA(buf, &size);
#else
    gethostname(buf, sizeof(buf));
#endif
    std::string name = buf;
    if (name.empty()) name = "stayputvr-host";
    return name;
}

// Rank an IPv4 by how likely it is to be the real LAN adapter a local client
// (VRChat) can actually reach. The host often also has VPN (Tailscale CGNAT
// 100.64/10), WSL/Docker/Hyper-V (172.16/12), and link-local (169.254)
// addresses; advertising one of those as our OSCQuery A-record makes VRChat
// unable to connect, so it never discovers us. Higher score wins; <=0 = skip.
static int IPv4Score(const std::string& ip) {
    int a = 0, b = 0, c = 0, d = 0;
    if (std::sscanf(ip.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) != 4) return -1;
    if (a == 127) return -1;                       // loopback
    if (a == 169 && b == 254) return -1;           // link-local
    if (a == 192 && b == 168) return 100;          // typical home LAN
    if (a == 10) return 80;                        // private LAN
    if (a == 172 && b >= 16 && b <= 31) return 20; // private, but often WSL/Docker/Hyper-V
    if (a == 100 && b >= 64 && b <= 127) return 5; // CGNAT / Tailscale (last resort)
    return 50;                                     // other routable
}

// All usable local IPv4 addresses (one per interface), best LAN first. We
// advertise/browse on every one of these so discovery works regardless of which
// interface VRChat is on -- and, crucially, so a VPN like Tailscale being the
// OS default multicast interface can't hide us from the real LAN.
static std::vector<std::string> EnumerateLocalIPv4() {
    std::vector<std::string> ips;
    auto add = [&](const std::string& s) {
        if (IPv4Score(s) <= 0) return;
        if (std::find(ips.begin(), ips.end(), s) == ips.end()) ips.push_back(s);
    };
#ifdef _WIN32
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) return ips;
    struct addrinfo hints{}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(hostname, nullptr, &hints, &result) != 0) return ips;
    for (auto* p = result; p; p = p->ai_next) {
        char buf[INET_ADDRSTRLEN];
        auto* addr = reinterpret_cast<sockaddr_in*>(p->ai_addr);
        inet_ntop(AF_INET, &addr->sin_addr, buf, sizeof(buf));
        add(buf);
    }
    freeaddrinfo(result);
#else
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) return ips;
    for (auto* ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        char buf[INET_ADDRSTRLEN];
        auto* addr = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
        inet_ntop(AF_INET, &addr->sin_addr, buf, sizeof(buf));
        add(buf);
    }
    freeifaddrs(ifaddr);
#endif
    std::sort(ips.begin(), ips.end(), [](const std::string& a, const std::string& b){
        return IPv4Score(a) > IPv4Score(b);
    });
    return ips;
}

static std::string GetLocalIPv4() {
    auto ips = EnumerateLocalIPv4();
    return ips.empty() ? "127.0.0.1" : ips.front();
}

static int FindAvailableTCPPort() {
    int sock = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
    if (sock < 0) return 0;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return 0;
    }
    socklen_t len = sizeof(addr);
    getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len);
    int port = ntohs(addr.sin_port);
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    return port;
}

static json ValueToJSON(const std::variant<float, int, bool, std::string>& v) {
    if (auto* f = std::get_if<float>(&v)) return json::array({*f});
    if (auto* i = std::get_if<int>(&v)) return json::array({*i});
    if (auto* b = std::get_if<bool>(&v)) return json::array({*b});
    if (auto* s = std::get_if<std::string>(&v)) return json::array({*s});
    return json::array({0});
}

OSCQueryServer::OSCQueryServer() {
    hostname_ = GetLocalHostname();
}

OSCQueryServer::~OSCQueryServer() {
    Stop();
}

void OSCQueryServer::SetVRChatPortDiscoveredCallback(std::function<void(int)> cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    vrc_port_discovered_callback_ = std::move(cb);
}

bool OSCQueryServer::Start(int osc_udp_port) {
    if (running_) return true;

    osc_port_ = osc_udp_port;
    http_port_ = FindAvailableTCPPort();
    if (http_port_ == 0) {
        LogError("OSCQuery: failed to find available TCP port");
        return false;
    }

    running_ = true;

    http_thread_ = std::thread(&OSCQueryServer::HTTPThread, this);
    mdns_browse_thread_ = std::thread(&OSCQueryServer::MDNSBrowseThread, this);
    mdns_listen_thread_ = std::thread(&OSCQueryServer::MDNSListenThread, this);

    LogInfo("OSCQuery started: HTTP port=" + std::to_string(http_port_) +
            " OSC port=" + std::to_string(osc_port_));
    return true;
}

void OSCQueryServer::Stop() {
    if (!running_) return;
    running_ = false;

    if (http_server_) {
        static_cast<httplib::Server*>(http_server_)->stop();
    }

    {
        std::lock_guard<std::mutex> lock(mdns_sockets_mutex_);
        for (int s : mdns_sockets_) if (s >= 0) mdns_socket_close(s);
        mdns_sockets_.clear();
    }

    if (http_thread_.joinable()) http_thread_.join();
    if (mdns_browse_thread_.joinable()) mdns_browse_thread_.join();
    if (mdns_listen_thread_.joinable()) mdns_listen_thread_.join();

    http_server_ = nullptr;
    LogInfo("OSCQuery stopped");
}

void OSCQueryServer::AddParameter(const std::string& path, const std::string& osc_type,
                                   Access access, std::variant<float, int, bool, std::string> initial_value) {
    std::lock_guard<std::mutex> lock(param_mutex_);
    auto it = param_index_.find(path);
    if (it != param_index_.end()) {
        params_[it->second].osc_type = osc_type;
        params_[it->second].access = access;
        params_[it->second].value = initial_value;
        return;
    }
    param_index_[path] = params_.size();
    params_.push_back({path, osc_type, access, initial_value});
}

void OSCQueryServer::UpdateValue(const std::string& path, std::variant<float, int, bool, std::string> value) {
    std::lock_guard<std::mutex> lock(param_mutex_);
    auto it = param_index_.find(path);
    if (it != param_index_.end()) {
        params_[it->second].value = value;
    }
}

std::optional<int> OSCQueryServer::GetVRChatOSCPort() const {
    std::lock_guard<std::mutex> lock(vrc_mutex_);
    return vrc_osc_port_;
}

std::optional<int> OSCQueryServer::GetVRChatQueryPort() const {
    std::lock_guard<std::mutex> lock(vrc_mutex_);
    return vrc_query_port_;
}

bool OSCQueryServer::IsVRChatConnected() const {
    std::lock_guard<std::mutex> lock(vrc_mutex_);
    return vrc_osc_port_.has_value();
}

std::string OSCQueryServer::BuildHostInfo() const {
    json j;
    j["NAME"] = service_name_;
    j["OSC_PORT"] = osc_port_;
    j["OSC_TRANSPORT"] = "UDP";
    j["EXTENSIONS"]["ACCESS"] = true;
    j["EXTENSIONS"]["VALUE"] = true;
    return j.dump();
}

static json MakeParamJSON(const OSCQueryServer::ParamNode& p) {
    json node;
    node["FULL_PATH"] = p.full_path;
    node["TYPE"] = p.osc_type;
    node["ACCESS"] = static_cast<int>(p.access);
    node["VALUE"] = ValueToJSON(p.value);
    return node;
}

static json MakeContainerJSON(const std::string& path) {
    json node;
    node["FULL_PATH"] = path;
    node["CONTENTS"] = json::object();
    return node;
}

std::string OSCQueryServer::BuildFullTree() const {
    std::lock_guard<std::mutex> lock(param_mutex_);

    json root = MakeContainerJSON("/");

    for (auto& p : params_) {
        std::vector<std::string> segments;
        std::istringstream ss(p.full_path.substr(1));
        std::string seg;
        while (std::getline(ss, seg, '/')) {
            if (!seg.empty()) segments.push_back(seg);
        }

        json* current = &root;
        std::string built_path;
        for (size_t i = 0; i < segments.size(); i++) {
            built_path += "/" + segments[i];
            bool is_leaf = (i == segments.size() - 1);

            if (!current->contains("CONTENTS")) {
                (*current)["CONTENTS"] = json::object();
            }

            auto& contents = (*current)["CONTENTS"];
            if (!contents.contains(segments[i])) {
                if (is_leaf) {
                    contents[segments[i]] = MakeParamJSON(p);
                } else {
                    contents[segments[i]] = MakeContainerJSON(built_path);
                }
            }
            current = &contents[segments[i]];
        }
    }

    return root.dump();
}

std::string OSCQueryServer::BuildNodeJSON(const std::string& path) const {
    std::lock_guard<std::mutex> lock(param_mutex_);
    auto it = param_index_.find(path);
    if (it != param_index_.end()) {
        return MakeParamJSON(params_[it->second]).dump();
    }

    json container = MakeContainerJSON(path);
    bool found = false;
    for (auto& p : params_) {
        if (p.full_path.rfind(path, 0) == 0 && p.full_path.size() > path.size()) {
            std::string remainder = p.full_path.substr(path.size());
            if (remainder[0] == '/') remainder = remainder.substr(1);
            auto slash = remainder.find('/');
            std::string child = (slash != std::string::npos) ? remainder.substr(0, slash) : remainder;

            if (!container["CONTENTS"].contains(child)) {
                if (slash == std::string::npos) {
                    container["CONTENTS"][child] = MakeParamJSON(p);
                } else {
                    std::string child_path = path + "/" + child;
                    container["CONTENTS"][child] = MakeContainerJSON(child_path);
                }
                found = true;
            }
        }
    }

    if (found) return container.dump();
    return "";
}

void OSCQueryServer::HTTPThread() {
    auto server_ptr = std::make_unique<httplib::Server>();
    auto* server = server_ptr.get();
    http_server_ = server;

    server->Get("/", [this](const httplib::Request& req, httplib::Response& res) {
        LogInfo("OSCQuery HTTP GET / " + std::string(req.has_param("HOST_INFO") ? "?HOST_INFO " : "") +
                "from " + req.remote_addr + " (this means a client -- e.g. VRChat -- found us)");
        if (req.has_param("HOST_INFO")) {
            res.set_content(BuildHostInfo(), "application/json");
        } else {
            res.set_content(BuildFullTree(), "application/json");
        }
    });

    server->Get(".*", [this](const httplib::Request& req, httplib::Response& res) {
        std::string path = req.path;
        LogDebug("OSCQuery HTTP GET " + path + " from " + req.remote_addr);
        if (path == "/") return;

        if (req.has_param("HOST_INFO")) {
            res.set_content(BuildHostInfo(), "application/json");
            return;
        }

        std::string body = BuildNodeJSON(path);
        if (body.empty()) {
            res.status = 404;
            res.set_content("{}", "application/json");
        } else {
            res.set_content(body, "application/json");
        }
    });

    LogInfo("OSCQuery HTTP server listening on port " + std::to_string(http_port_));
    server->listen("0.0.0.0", http_port_);

    server_ptr.reset();
    http_server_ = nullptr;
}

// --- mDNS Browse (discover VRChat) ---

struct BrowseContext {
    OSCQueryServer* self;
    std::optional<int> osc_port;
    std::optional<int> query_port;
};

static int MDNSBrowseCallback(int sock, const struct sockaddr* from, size_t addrlen,
                               mdns_entry_type_t entry, uint16_t query_id, uint16_t rtype,
                               uint16_t rclass, uint32_t ttl, const void* data, size_t size,
                               size_t name_offset, size_t name_length, size_t record_offset,
                               size_t record_length, void* user_data) {
    auto* ctx = static_cast<BrowseContext*>(user_data);
    if (rtype != MDNS_RECORDTYPE_SRV) return 0;

    char name_buf[256] = {};
    mdns_string_t name = mdns_string_extract(data, size, &name_offset, name_buf, sizeof(name_buf));
    std::string service_name(name.str, name.length);

    mdns_record_srv_t srv = mdns_record_parse_srv(data, size, record_offset, record_length,
                                                   name_buf, sizeof(name_buf));

    if (service_name.find("_osc._udp") != std::string::npos &&
        service_name.find("VRChat") != std::string::npos) {
        ctx->osc_port = srv.port;
        LogInfo("OSCQuery: found VRChat OSC at port " + std::to_string(srv.port));
    }
    else if (service_name.find("_oscjson._tcp") != std::string::npos &&
             service_name.find("VRChat") != std::string::npos) {
        ctx->query_port = srv.port;
        LogInfo("OSCQuery: found VRChat OSCQuery at port " + std::to_string(srv.port));
    }

    return 0;
}

void OSCQueryServer::MDNSBrowseThread() {
    LogInfo("OSCQuery mDNS browse thread started");

    // After this many consecutive browse cycles without seeing VRChat's
    // _osc._udp service, assume VRChat has gone away and clear the discovered
    // port. This lets a VRChat restart on the *same* port be re-detected: with
    // vrc_osc_port_ cleared, the next discovery differs from the stored value
    // and fires the retarget callback again. At ~7s per cycle, 3 misses is
    // roughly 20s of silence before we declare VRChat gone.
    constexpr int MAX_BROWSE_MISSES = 3;
    int consecutive_misses = 0;

    while (running_) {
        // Query on every interface (bind each socket to a specific interface so
        // the query egresses it), so we find VRChat regardless of which interface
        // it advertises on -- not just the OS default (which a VPN can own).
        std::vector<int> socks;
        for (const auto& ip : EnumerateLocalIPv4()) {
            struct sockaddr_in saddr{};
            saddr.sin_family = AF_INET;
            saddr.sin_port = 0; // ephemeral source port on this interface
            inet_pton(AF_INET, ip.c_str(), &saddr.sin_addr);
            int sock = mdns_socket_open_ipv4(&saddr);
            if (sock >= 0) socks.push_back(sock);
        }
        if (socks.empty()) {
            LogWarning("OSCQuery: failed to open any mDNS query socket");
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        BrowseContext ctx{this, std::nullopt, std::nullopt};
        char buffer[2048];

        mdns_query_t queries[2] = {
            {MDNS_RECORDTYPE_PTR, MDNS_STRING_CONST("_oscjson._tcp.local.")},
            {MDNS_RECORDTYPE_PTR, MDNS_STRING_CONST("_osc._udp.local.")}
        };

        for (int sock : socks) {
#ifdef _WIN32
            DWORD tv = 200;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
            struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 200000;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
            mdns_multiquery_send(sock, queries, 2, buffer, sizeof(buffer), 0);
        }

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline && running_) {
            for (int sock : socks)
                mdns_query_recv(sock, buffer, sizeof(buffer), MDNSBrowseCallback, &ctx, 0);
        }

        for (int sock : socks) mdns_socket_close(sock);

        if (ctx.osc_port || ctx.query_port) {
            bool osc_port_changed = false;
            int new_osc_port = 0;
            {
                std::lock_guard<std::mutex> lock(vrc_mutex_);
                if (ctx.osc_port && vrc_osc_port_ != ctx.osc_port) {
                    vrc_osc_port_ = ctx.osc_port;
                    osc_port_changed = true;
                    new_osc_port = *ctx.osc_port;
                }
                if (ctx.query_port) vrc_query_port_ = ctx.query_port;
            }

            // Notify outside the lock so the callback can safely retarget the
            // OSC send socket.
            if (osc_port_changed) {
                std::function<void(int)> cb;
                {
                    std::lock_guard<std::mutex> lock(callback_mutex_);
                    cb = vrc_port_discovered_callback_;
                }
                if (cb) cb(new_osc_port);
            }
        }

        // Track VRChat presence based on whether its OSC service was seen this
        // cycle. On a sustained run of misses, forget the discovered port so a
        // same-port restart is treated as a fresh discovery next time around.
        if (ctx.osc_port) {
            consecutive_misses = 0;
        } else if (++consecutive_misses >= MAX_BROWSE_MISSES) {
            bool had_port = false;
            {
                std::lock_guard<std::mutex> lock(vrc_mutex_);
                had_port = vrc_osc_port_.has_value();
                vrc_osc_port_.reset();
                vrc_query_port_.reset();
            }
            if (had_port) {
                LogInfo("OSCQuery: VRChat no longer advertising its OSC service; "
                        "cleared discovered port (will retarget on rediscovery)");
            }
            consecutive_misses = 0;
        }

        for (int i = 0; i < 50 && running_; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    LogInfo("OSCQuery mDNS browse thread stopped");
}

// --- mDNS Listen (announce our services) ---

struct ListenContext {
    OSCQueryServer* self;
    int sock;
    std::string service_name;
    std::string hostname;
    int http_port;
    int osc_port;
    struct sockaddr_in local_addr;
};

static int MDNSListenCallback(int sock, const struct sockaddr* from, size_t addrlen,
                               mdns_entry_type_t entry, uint16_t query_id, uint16_t rtype,
                               uint16_t rclass, uint32_t ttl, const void* data, size_t size,
                               size_t name_offset, size_t name_length, size_t record_offset,
                               size_t record_length, void* user_data) {
    auto* ctx = static_cast<ListenContext*>(user_data);

    if (entry != MDNS_ENTRYTYPE_QUESTION) return 0;

    char name_buf[256] = {};
    mdns_string_t name = mdns_string_extract(data, size, &name_offset, name_buf, sizeof(name_buf));
    std::string query_name(name.str, name.length);

    char sendbuf[2048];

    std::string oscjson_service = ctx->service_name + "._oscjson._tcp.local.";
    std::string osc_service = ctx->service_name + "._osc._udp.local.";
    std::string host = ctx->hostname + ".local.";

    if (query_name.find("_oscjson._tcp.local") != std::string::npos) {
        mdns_record_t answer = {};
        answer.name = {MDNS_STRING_CONST("_oscjson._tcp.local.")};
        answer.type = MDNS_RECORDTYPE_PTR;
        answer.data.ptr.name = {oscjson_service.c_str(), oscjson_service.size()};
        answer.rclass = 0;
        answer.ttl = 120;

        mdns_record_t additional[2] = {};
        additional[0].name = {oscjson_service.c_str(), oscjson_service.size()};
        additional[0].type = MDNS_RECORDTYPE_SRV;
        additional[0].data.srv.name = {host.c_str(), host.size()};
        additional[0].data.srv.port = static_cast<uint16_t>(ctx->http_port);
        additional[0].data.srv.priority = 0;
        additional[0].data.srv.weight = 0;
        additional[0].rclass = 0;
        additional[0].ttl = 120;

        additional[1].name = {host.c_str(), host.size()};
        additional[1].type = MDNS_RECORDTYPE_A;
        additional[1].data.a.addr = ctx->local_addr;
        additional[1].rclass = 0;
        additional[1].ttl = 120;

        mdns_query_answer_multicast(sock, sendbuf, sizeof(sendbuf),
                                     answer, nullptr, 0, additional, 2);
        LogDebug("OSCQuery: answered mDNS query for _oscjson._tcp.local (asker wants our OSCQuery service)");
    }

    if (query_name.find("_osc._udp.local") != std::string::npos) {
        mdns_record_t answer = {};
        answer.name = {MDNS_STRING_CONST("_osc._udp.local.")};
        answer.type = MDNS_RECORDTYPE_PTR;
        answer.data.ptr.name = {osc_service.c_str(), osc_service.size()};
        answer.rclass = 0;
        answer.ttl = 120;

        mdns_record_t additional[2] = {};
        additional[0].name = {osc_service.c_str(), osc_service.size()};
        additional[0].type = MDNS_RECORDTYPE_SRV;
        additional[0].data.srv.name = {host.c_str(), host.size()};
        additional[0].data.srv.port = static_cast<uint16_t>(ctx->osc_port);
        additional[0].data.srv.priority = 0;
        additional[0].data.srv.weight = 0;
        additional[0].rclass = 0;
        additional[0].ttl = 120;

        additional[1].name = {host.c_str(), host.size()};
        additional[1].type = MDNS_RECORDTYPE_A;
        additional[1].data.a.addr = ctx->local_addr;
        additional[1].rclass = 0;
        additional[1].ttl = 120;

        mdns_query_answer_multicast(sock, sendbuf, sizeof(sendbuf),
                                     answer, nullptr, 0, additional, 2);
        LogDebug("OSCQuery: answered mDNS query for _osc._udp.local");
    }

    return 0;
}

// Announce (or, with ttl 0, retract) our two services on one socket, using the
// given interface address for the SRV target's A record.
static void AnnounceOnSocket(int sock, const std::string& service_name,
                             const std::string& hostname, int http_port, int osc_port,
                             const struct sockaddr_in& local_addr, bool goodbye) {
    char sendbuf[2048];
    std::string oscjson_service = service_name + "._oscjson._tcp.local.";
    std::string osc_service = service_name + "._osc._udp.local.";
    std::string host = hostname + ".local.";
    const uint32_t ttl = goodbye ? 0 : 120;

    auto send_one = [&](const std::string& svc, int port) {
        mdns_record_t answer = {};
        answer.name = {svc.c_str(), svc.size()};
        answer.type = MDNS_RECORDTYPE_SRV;
        answer.data.srv.name = {host.c_str(), host.size()};
        answer.data.srv.port = static_cast<uint16_t>(port);
        answer.data.srv.priority = 0;
        answer.data.srv.weight = 0;
        answer.rclass = 0;
        answer.ttl = ttl;

        mdns_record_t additional = {};
        additional.name = {host.c_str(), host.size()};
        additional.type = MDNS_RECORDTYPE_A;
        additional.data.a.addr = local_addr;
        additional.rclass = 0;
        additional.ttl = ttl;

        if (goodbye)
            mdns_goodbye_multicast(sock, sendbuf, sizeof(sendbuf), answer, nullptr, 0, &additional, 1);
        else
            mdns_announce_multicast(sock, sendbuf, sizeof(sendbuf), answer, nullptr, 0, &additional, 1);
    };

    send_one(oscjson_service, http_port);
    send_one(osc_service, osc_port);
}

void OSCQueryServer::MDNSListenThread() {
    LogInfo("OSCQuery mDNS listen thread started");

    // Open one socket per local interface so the multicast announcement/answer
    // egresses *every* interface (the mdns lib only pins IP_MULTICAST_IF when a
    // socket is bound to a specific interface address). Binding INADDR_ANY would
    // send out only the OS default interface -- which a VPN like Tailscale can
    // own, hiding us from the real LAN where VRChat is.
    int bind_attempts = 0;
    while (running_) {
        std::vector<std::string> ips = EnumerateLocalIPv4();
        std::vector<int> socks;
        std::vector<ListenContext> ctxs;
        ctxs.reserve(ips.size());

        for (const auto& ip : ips) {
            struct sockaddr_in saddr{};
            saddr.sin_family = AF_INET;
            saddr.sin_port = htons(MDNS_PORT);
            inet_pton(AF_INET, ip.c_str(), &saddr.sin_addr);
            int sock = mdns_socket_open_ipv4(&saddr);
            if (sock < 0) continue;

            ListenContext ctx{};
            ctx.self = this;
            ctx.sock = sock;
            ctx.service_name = service_name_;
            ctx.hostname = hostname_;
            ctx.http_port = http_port_;
            ctx.osc_port = osc_port_;
            ctx.local_addr = saddr;
            ctx.local_addr.sin_port = 0; // A record carries no port
            socks.push_back(sock);
            ctxs.push_back(ctx);
        }

        if (socks.empty()) {
            if (bind_attempts == 0) {
                LogError("OSCQuery: could not bind the mDNS port on any interface (UDP 5353 in use, "
                         "e.g. by avahi/Bonjour or another OSC app). VRChat will NOT discover "
                         "StayPutVR. Turn off \"OSC Query\" to use manual ports, or free UDP 5353. "
                         "Retrying every 5s...");
            } else if (bind_attempts % 12 == 0) {
                LogWarning("OSCQuery: still unable to bind mDNS UDP 5353; discovery unavailable.");
            }
            ++bind_attempts;
            mdns_advertising_ = false;
            for (int i = 0; i < 50 && running_; i++)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (bind_attempts > 0) LogInfo("OSCQuery: mDNS port bound after retry; advertising resumed");
        bind_attempts = 0;
        {
            std::lock_guard<std::mutex> lock(mdns_sockets_mutex_);
            mdns_sockets_ = socks;
        }
        mdns_advertising_ = true;

        std::string ip_list;
        for (size_t i = 0; i < ips.size() && i < socks.size(); ++i)
            ip_list += (i ? ", " : "") + ips[i];
        LogInfo("OSCQuery: advertising as '" + service_name_ + "' host " + hostname_ + ".local on " +
                std::to_string(socks.size()) + " interface(s) [" + ip_list + "] (oscjson tcp:" +
                std::to_string(http_port_) + ", osc udp:" + std::to_string(osc_port_) +
                "). Watch for an HTTP GET line when VRChat connects.");

        // Initial announcement on every interface, then set a short recv timeout
        // so the listen loop stays responsive to shutdown.
        for (size_t i = 0; i < socks.size(); ++i) {
            AnnounceOnSocket(socks[i], service_name_, hostname_, http_port_, osc_port_,
                             ctxs[i].local_addr, /*goodbye=*/false);
#ifdef _WIN32
            DWORD tv = 250;
            setsockopt(socks[i], SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
            struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 250000;
            setsockopt(socks[i], SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
        }
        LogInfo("OSCQuery: announced services (oscjson tcp:" + std::to_string(http_port_) +
                " osc udp:" + std::to_string(osc_port_) + ")");

        char buffer[2048];
        while (running_) {
            for (size_t i = 0; i < socks.size(); ++i)
                mdns_socket_listen(socks[i], buffer, sizeof(buffer), MDNSListenCallback, &ctxs[i]);
        }

        // Retract + close. Take ownership under the lock so Stop() doesn't also
        // close these (double-close), then say goodbye on each interface.
        std::vector<int> to_close;
        {
            std::lock_guard<std::mutex> lock(mdns_sockets_mutex_);
            to_close.swap(mdns_sockets_);
        }
        for (size_t i = 0; i < to_close.size(); ++i) {
            struct sockaddr_in la = (i < ctxs.size()) ? ctxs[i].local_addr : sockaddr_in{};
            AnnounceOnSocket(to_close[i], service_name_, hostname_, http_port_, osc_port_, la,
                             /*goodbye=*/true);
            mdns_socket_close(to_close[i]);
        }
        mdns_advertising_ = false;
        break; // running_ became false; leave the retry loop
    }

    LogInfo("OSCQuery mDNS listen thread stopped");
}

} // namespace StayPutVR
