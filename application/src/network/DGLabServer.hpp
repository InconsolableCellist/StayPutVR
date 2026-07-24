#pragma once

// Minimal embedded WebSocket server for the DG-Lab Coyote integration.
//
// The DG-Lab phone app scans a QR code containing ws://<lan-ip>:<port>/<id>
// and connects to us; the phone then relays our commands to the Coyote over
// Bluetooth. We only ever need a single client (one phone), text frames only,
// no TLS (plain ws:// on the LAN, same as the official DG-Lab samples).
//
// Threading: one background thread owns accept + receive. Inbound traffic and
// connection state changes are queued as events and drained on the main thread
// via DrainEvents() (same pump-on-Update pattern as WebSocketClient). SendText
// is safe to call from the main thread while the receive thread runs.

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace StayPutVR {

    class DGLabServer {
    public:
        enum class EventType {
            ClientConnected,     // WebSocket handshake completed
            ClientDisconnected,
            TextMessage
        };

        struct Event {
            EventType type;
            std::string payload; // message text, or disconnect reason
        };

        DGLabServer() = default;
        ~DGLabServer();

        DGLabServer(const DGLabServer&) = delete;
        DGLabServer& operator=(const DGLabServer&) = delete;

        bool Start(int port);
        void Stop();

        bool IsRunning() const { return running_; }
        bool HasClient() const { return client_connected_; }
        int GetPort() const { return port_; }
        std::string GetLastError() const;

        // Send a text frame to the connected client. Returns false if no
        // client is connected or the send failed (the receive thread will
        // surface the disconnect as an event).
        bool SendText(const std::string& payload);

        // Main-thread pump: returns all queued events since the last call.
        std::vector<Event> DrainEvents();

    private:
        void ServerLoop();
        void ServeClient(std::uintptr_t client_socket);
        bool PerformHandshake(std::uintptr_t client_socket);
        bool SendFrame(std::uintptr_t client_socket, uint8_t opcode, const std::string& payload);
        void PushEvent(EventType type, const std::string& payload = "");
        void SetError(const std::string& error);
        void CloseClient();

        std::atomic<bool> running_{false};
        std::atomic<bool> client_connected_{false};
        int port_ = 0;

        // Sockets stored as uintptr_t so the header stays platform-neutral
        // (SOCKET on Windows is a UINT_PTR, int on POSIX).
        std::atomic<std::uintptr_t> listen_socket_;
        std::atomic<std::uintptr_t> client_socket_;

        std::unique_ptr<std::thread> thread_;

        mutable std::mutex send_mutex_;
        mutable std::mutex event_mutex_;
        std::queue<Event> events_;

        mutable std::mutex error_mutex_;
        std::string last_error_;
    };

} // namespace StayPutVR
