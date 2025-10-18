#pragma once

#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <queue>
#include <Windows.h>
#include <winhttp.h>

namespace StayPutVR {

// WebSocket connection states
enum class WebSocketState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    DISCONNECTING,
    ERROR_STATE
};

// Callback types
using OnMessageCallback = std::function<void(const std::string& message)>;
using OnConnectedCallback = std::function<void()>;
using OnDisconnectedCallback = std::function<void(const std::string& reason)>;
using OnErrorCallback = std::function<void(const std::string& error)>;

class WebSocketClient {
public:
    WebSocketClient();
    ~WebSocketClient();

    // Connection management
    bool Connect(const std::string& url);
    void Disconnect();
    bool IsConnected() const;
    WebSocketState GetState() const { return state_; }
    
    // Send message
    bool SendText(const std::string& message);
    bool SendBinary(const void* data, size_t length);
    
    // Update - call this regularly to process messages
    void Update();
    
    // Callbacks
    void SetOnMessageCallback(OnMessageCallback callback) { on_message_ = callback; }
    void SetOnConnectedCallback(OnConnectedCallback callback) { on_connected_ = callback; }
    void SetOnDisconnectedCallback(OnDisconnectedCallback callback) { on_disconnected_ = callback; }
    void SetOnErrorCallback(OnErrorCallback callback) { on_error_ = callback; }
    
    // Get last error
    std::string GetLastError() const;

private:
    // WebSocket handles
    HINTERNET h_session_;
    HINTERNET h_connection_;
    HINTERNET h_websocket_;
    
    // State
    std::atomic<WebSocketState> state_;
    std::string last_error_;
    mutable std::mutex error_mutex_;
    
    // Connection info
    std::string url_;
    std::wstring host_;
    std::wstring path_;
    int port_;
    bool secure_;
    
    // Message queue for received messages
    std::queue<std::string> message_queue_;
    std::mutex queue_mutex_;
    
    // Callbacks
    OnMessageCallback on_message_;
    OnConnectedCallback on_connected_;
    OnDisconnectedCallback on_disconnected_;
    OnErrorCallback on_error_;
    
    // Background thread for receiving
    std::thread receive_thread_;
    std::atomic<bool> receive_thread_running_;
    
    // Helper methods
    bool ParseUrl(const std::string& url);
    void SetError(const std::string& error);
    void ReceiveThreadFunction();
    void CleanupHandles();
};

} // namespace StayPutVR

