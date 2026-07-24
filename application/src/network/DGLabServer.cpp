#include "DGLabServer.hpp"

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
#include <array>
#include <cctype>
#include <cstring>

namespace StayPutVR {

namespace {

    const std::uintptr_t kInvalidSock = static_cast<std::uintptr_t>(INVALID_SOCKET);

    // --- SHA-1 (needed only for the WebSocket handshake accept key) ---
    struct Sha1 {
        static std::array<uint8_t, 20> Hash(const std::string& input) {
            uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};

            std::vector<uint8_t> msg(input.begin(), input.end());
            const uint64_t bit_len = static_cast<uint64_t>(msg.size()) * 8;
            msg.push_back(0x80);
            while (msg.size() % 64 != 56) msg.push_back(0x00);
            for (int i = 7; i >= 0; --i) msg.push_back(static_cast<uint8_t>(bit_len >> (i * 8)));

            for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
                uint32_t w[80];
                for (int i = 0; i < 16; ++i) {
                    w[i] = (static_cast<uint32_t>(msg[chunk + i * 4]) << 24) |
                           (static_cast<uint32_t>(msg[chunk + i * 4 + 1]) << 16) |
                           (static_cast<uint32_t>(msg[chunk + i * 4 + 2]) << 8) |
                           static_cast<uint32_t>(msg[chunk + i * 4 + 3]);
                }
                auto rol = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
                for (int i = 16; i < 80; ++i)
                    w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

                uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
                for (int i = 0; i < 80; ++i) {
                    uint32_t f, k;
                    if (i < 20)      { f = (b & c) | (~b & d);           k = 0x5A827999; }
                    else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1; }
                    else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDC; }
                    else             { f = b ^ c ^ d;                    k = 0xCA62C1D6; }
                    uint32_t tmp = rol(a, 5) + f + e + k + w[i];
                    e = d; d = c; c = rol(b, 30); b = a; a = tmp;
                }
                h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
            }

            std::array<uint8_t, 20> out{};
            for (int i = 0; i < 5; ++i) {
                out[i * 4]     = static_cast<uint8_t>(h[i] >> 24);
                out[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
                out[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
                out[i * 4 + 3] = static_cast<uint8_t>(h[i]);
            }
            return out;
        }
    };

    std::string Base64Encode(const uint8_t* data, size_t len) {
        static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((len + 2) / 3) * 4);
        for (size_t i = 0; i < len; i += 3) {
            uint32_t n = static_cast<uint32_t>(data[i]) << 16;
            if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
            if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);
            out.push_back(tbl[(n >> 18) & 63]);
            out.push_back(tbl[(n >> 12) & 63]);
            out.push_back(i + 1 < len ? tbl[(n >> 6) & 63] : '=');
            out.push_back(i + 2 < len ? tbl[n & 63] : '=');
        }
        return out;
    }

    std::string ComputeAcceptKey(const std::string& client_key) {
        auto digest = Sha1::Hash(client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
        return Base64Encode(digest.data(), digest.size());
    }

} // namespace

DGLabServer::~DGLabServer() {
    Stop();
}

bool DGLabServer::Start(int port) {
    if (running_) return true;

    port_ = port;
    listen_socket_ = kInvalidSock;
    client_socket_ = kInvalidSock;

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        SetError("WSAStartup failed");
        return false;
    }
#endif

    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == INVALID_SOCKET) {
        SetError("Failed to create listen socket");
        return false;
    }

    // Allow quick restart after Stop() without lingering TIME_WAIT bind errors.
    int reuse = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<unsigned short>(port));

    if (bind(listen_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR ||
        listen(listen_sock, 2) == SOCKET_ERROR) {
        SetError("Failed to bind/listen on port " + std::to_string(port) +
                 " (port already in use?)");
        closesocket(listen_sock);
        return false;
    }

    listen_socket_ = static_cast<std::uintptr_t>(listen_sock);
    running_ = true;
    thread_ = std::make_unique<std::thread>(&DGLabServer::ServerLoop, this);

    if (Logger::IsInitialized())
        Logger::Info("DGLabServer: listening on port " + std::to_string(port));
    return true;
}

void DGLabServer::Stop() {
    if (!running_) return;
    running_ = false;

    // Closing the sockets unblocks select/recv in the server thread.
    CloseClient();
    SOCKET ls = static_cast<SOCKET>(listen_socket_.load());
    if (ls != INVALID_SOCKET) {
        listen_socket_ = kInvalidSock;
        closesocket(ls);
    }

    if (thread_ && thread_->joinable()) thread_->join();
    thread_.reset();
    client_connected_ = false;

#ifdef _WIN32
    WSACleanup(); // balanced with the WSAStartup in Start(); refcounted by Winsock
#endif

    if (Logger::IsInitialized())
        Logger::Info("DGLabServer: stopped");
}

std::string DGLabServer::GetLastError() const {
    std::lock_guard<std::mutex> lk(error_mutex_);
    return last_error_;
}

void DGLabServer::SetError(const std::string& error) {
    {
        std::lock_guard<std::mutex> lk(error_mutex_);
        last_error_ = error;
    }
    if (Logger::IsInitialized())
        Logger::Error("DGLabServer: " + error);
}

void DGLabServer::PushEvent(EventType type, const std::string& payload) {
    std::lock_guard<std::mutex> lk(event_mutex_);
    events_.push(Event{type, payload});
}

std::vector<DGLabServer::Event> DGLabServer::DrainEvents() {
    std::vector<Event> out;
    std::lock_guard<std::mutex> lk(event_mutex_);
    while (!events_.empty()) {
        out.push_back(std::move(events_.front()));
        events_.pop();
    }
    return out;
}

void DGLabServer::CloseClient() {
    SOCKET cs = static_cast<SOCKET>(client_socket_.load());
    if (cs != INVALID_SOCKET) {
        client_socket_ = kInvalidSock;
        closesocket(cs);
    }
    client_connected_ = false;
}

bool DGLabServer::SendText(const std::string& payload) {
    std::uintptr_t cs = client_socket_.load();
    if (!client_connected_ || cs == kInvalidSock) return false;
    return SendFrame(cs, 0x1, payload);
}

bool DGLabServer::SendFrame(std::uintptr_t client_socket, uint8_t opcode, const std::string& payload) {
    // Server-to-client frames are unmasked (RFC 6455).
    std::string frame;
    frame.push_back(static_cast<char>(0x80 | opcode)); // FIN + opcode
    const size_t len = payload.size();
    if (len < 126) {
        frame.push_back(static_cast<char>(len));
    } else if (len <= 0xFFFF) {
        frame.push_back(126);
        frame.push_back(static_cast<char>((len >> 8) & 0xFF));
        frame.push_back(static_cast<char>(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i)
            frame.push_back(static_cast<char>((static_cast<uint64_t>(len) >> (i * 8)) & 0xFF));
    }
    frame += payload;

    std::lock_guard<std::mutex> lk(send_mutex_);
    SOCKET s = static_cast<SOCKET>(client_socket);
    size_t sent = 0;
    while (sent < frame.size()) {
        int n = send(s, frame.data() + sent, static_cast<int>(frame.size() - sent), 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool DGLabServer::PerformHandshake(std::uintptr_t client_socket) {
    SOCKET s = static_cast<SOCKET>(client_socket);

    // Give the handshake a bounded time to arrive.
#ifdef _WIN32
    DWORD timeout_ms = 5000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
#else
    timeval tv{5, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    std::string request;
    char buf[2048];
    while (request.find("\r\n\r\n") == std::string::npos) {
        if (request.size() > 16384) return false;
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        request.append(buf, static_cast<size_t>(n));
    }

    // Case-insensitive search for the Sec-WebSocket-Key header.
    std::string lower = request;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::string header = "sec-websocket-key:";
    size_t pos = lower.find(header);
    if (pos == std::string::npos) return false;
    pos += header.size();
    size_t eol = request.find("\r\n", pos);
    if (eol == std::string::npos) return false;
    std::string key = request.substr(pos, eol - pos);
    key.erase(0, key.find_first_not_of(" \t"));
    key.erase(key.find_last_not_of(" \t") + 1);
    if (key.empty()) return false;

    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + ComputeAcceptKey(key) + "\r\n"
        "\r\n";
    if (send(s, response.c_str(), static_cast<int>(response.size()), 0) <= 0) return false;

    // Back to no receive timeout; the serve loop uses select() for pacing.
#ifdef _WIN32
    timeout_ms = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
#else
    timeval tv0{0, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv0, sizeof(tv0));
#endif
    return true;
}

void DGLabServer::ServerLoop() {
    while (running_) {
        SOCKET ls = static_cast<SOCKET>(listen_socket_.load());
        if (ls == INVALID_SOCKET) break;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(ls, &readfds);
        timeval timeout{1, 0};
#ifdef _WIN32
        int nfds = 0; // ignored on Winsock
#else
        int nfds = static_cast<int>(ls) + 1;
#endif
        int result = select(nfds, &readfds, nullptr, nullptr, &timeout);
        if (!running_) break;
        if (result == SOCKET_ERROR) break;
        if (result == 0) continue;

        SOCKET client = accept(ls, nullptr, nullptr);
        if (client == INVALID_SOCKET || !running_) {
            if (client != INVALID_SOCKET) closesocket(client);
            continue;
        }

        if (!PerformHandshake(static_cast<std::uintptr_t>(client))) {
            if (Logger::IsInitialized())
                Logger::Warning("DGLabServer: WebSocket handshake failed, dropping connection");
            closesocket(client);
            continue;
        }

        // Only one phone at a time; a fresh scan replaces the old link.
        CloseClient();
        client_socket_ = static_cast<std::uintptr_t>(client);
        client_connected_ = true;
        PushEvent(EventType::ClientConnected);

        ServeClient(static_cast<std::uintptr_t>(client));

        if (client_socket_.load() == static_cast<std::uintptr_t>(client)) {
            CloseClient();
        }
        PushEvent(EventType::ClientDisconnected);
    }
    running_ = false;
}

void DGLabServer::ServeClient(std::uintptr_t client_socket) {
    SOCKET cs = static_cast<SOCKET>(client_socket);
    SOCKET ls = static_cast<SOCKET>(listen_socket_.load());

    std::string buffer;    // raw bytes not yet parsed into frames
    std::string message;   // accumulated payload across continuation frames

    while (running_ && client_socket_.load() == client_socket) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(cs, &readfds);
        // Also watch the listen socket so a re-scan (new phone connection)
        // can replace this one without waiting for the old link to die.
        if (ls != INVALID_SOCKET) FD_SET(ls, &readfds);
        timeval timeout{1, 0};
#ifdef _WIN32
        int nfds = 0;
#else
        int nfds = static_cast<int>(std::max(cs, ls)) + 1;
#endif
        int result = select(nfds, &readfds, nullptr, nullptr, &timeout);
        if (!running_) return;
        if (result == SOCKET_ERROR) return;
        if (result == 0) continue;

        if (ls != INVALID_SOCKET && FD_ISSET(ls, &readfds)) {
            // New connection pending: return so ServerLoop accepts it and
            // replaces us.
            return;
        }
        if (!FD_ISSET(cs, &readfds)) continue;

        char buf[4096];
        int n = recv(cs, buf, sizeof(buf), 0);
        if (n <= 0) return; // closed or error
        buffer.append(buf, static_cast<size_t>(n));

        // Parse complete frames out of the buffer.
        while (true) {
            if (buffer.size() < 2) break;
            const uint8_t b0 = static_cast<uint8_t>(buffer[0]);
            const uint8_t b1 = static_cast<uint8_t>(buffer[1]);
            const bool fin = (b0 & 0x80) != 0;
            const uint8_t opcode = b0 & 0x0F;
            const bool masked = (b1 & 0x80) != 0;
            uint64_t payload_len = b1 & 0x7F;
            size_t header_len = 2;

            if (payload_len == 126) {
                if (buffer.size() < 4) break;
                payload_len = (static_cast<uint64_t>(static_cast<uint8_t>(buffer[2])) << 8) |
                              static_cast<uint8_t>(buffer[3]);
                header_len = 4;
            } else if (payload_len == 127) {
                if (buffer.size() < 10) break;
                payload_len = 0;
                for (int i = 0; i < 8; ++i)
                    payload_len = (payload_len << 8) | static_cast<uint8_t>(buffer[2 + i]);
                header_len = 10;
            }
            if (payload_len > 1 << 20) return; // absurd for this protocol; bail

            size_t mask_len = masked ? 4 : 0;
            if (buffer.size() < header_len + mask_len + payload_len) break;

            uint8_t mask[4] = {0, 0, 0, 0};
            if (masked)
                std::memcpy(mask, buffer.data() + header_len, 4);

            std::string payload = buffer.substr(header_len + mask_len,
                                                static_cast<size_t>(payload_len));
            if (masked) {
                for (size_t i = 0; i < payload.size(); ++i)
                    payload[i] = static_cast<char>(
                        static_cast<uint8_t>(payload[i]) ^ mask[i % 4]);
            }
            buffer.erase(0, header_len + mask_len + static_cast<size_t>(payload_len));

            switch (opcode) {
                case 0x0: // continuation
                case 0x1: // text
                    message += payload;
                    if (fin) {
                        PushEvent(EventType::TextMessage, message);
                        message.clear();
                    }
                    break;
                case 0x8: // close
                    SendFrame(client_socket, 0x8, "");
                    return;
                case 0x9: // ping
                    SendFrame(client_socket, 0xA, payload);
                    break;
                default:  // pong / binary: ignore
                    break;
            }
        }
    }
}

} // namespace StayPutVR
