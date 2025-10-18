#include "WebSocketClient.hpp"
#include "Logger.hpp"
#include <sstream>
#include <algorithm>

#pragma comment(lib, "winhttp.lib")

namespace StayPutVR {

// Helper function to convert DWORD to string
static std::string DWordToString(DWORD value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

// Helper function to convert UTF-8 to wide string
static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

// Helper function to convert wide string to UTF-8
static std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

WebSocketClient::WebSocketClient()
    : h_session_(nullptr)
    , h_connection_(nullptr)
    , h_websocket_(nullptr)
    , state_(WebSocketState::DISCONNECTED)
    , port_(0)
    , secure_(false)
    , receive_thread_running_(false)
{
}

WebSocketClient::~WebSocketClient() {
    Disconnect();
}

bool WebSocketClient::ParseUrl(const std::string& url) {
    // Parse WebSocket URL (ws:// or wss://)
    url_ = url;
    
    // Check protocol
    if (url.find("wss://") == 0) {
        secure_ = true;
        size_t start = 6; // Length of "wss://"
        
        // Find path separator
        size_t path_pos = url.find('/', start);
        std::string host_port;
        
        if (path_pos != std::string::npos) {
            host_port = url.substr(start, path_pos - start);
            path_ = Utf8ToWide(url.substr(path_pos));
        } else {
            host_port = url.substr(start);
            path_ = L"/";
        }
        
        // Parse host and port
        size_t port_pos = host_port.find(':');
        if (port_pos != std::string::npos) {
            host_ = Utf8ToWide(host_port.substr(0, port_pos));
            port_ = std::stoi(host_port.substr(port_pos + 1));
        } else {
            host_ = Utf8ToWide(host_port);
            port_ = INTERNET_DEFAULT_HTTPS_PORT;
        }
        
        return true;
    } else if (url.find("ws://") == 0) {
        secure_ = false;
        size_t start = 5; // Length of "ws://"
        
        size_t path_pos = url.find('/', start);
        std::string host_port;
        
        if (path_pos != std::string::npos) {
            host_port = url.substr(start, path_pos - start);
            path_ = Utf8ToWide(url.substr(path_pos));
        } else {
            host_port = url.substr(start);
            path_ = L"/";
        }
        
        size_t port_pos = host_port.find(':');
        if (port_pos != std::string::npos) {
            host_ = Utf8ToWide(host_port.substr(0, port_pos));
            port_ = std::stoi(host_port.substr(port_pos + 1));
        } else {
            host_ = Utf8ToWide(host_port);
            port_ = INTERNET_DEFAULT_HTTP_PORT;
        }
        
        return true;
    }
    
    SetError("Invalid WebSocket URL format. Must start with ws:// or wss://");
    return false;
}

bool WebSocketClient::Connect(const std::string& url) {
    if (state_ != WebSocketState::DISCONNECTED) {
        SetError("Already connected or connecting");
        return false;
    }
    
    state_ = WebSocketState::CONNECTING;
    
    // Parse URL
    if (!ParseUrl(url)) {
        state_ = WebSocketState::ERROR_STATE;
        return false;
    }
    
    // Open session
    h_session_ = WinHttpOpen(
        L"StayPutVR WebSocket Client/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    
    if (!h_session_) {
        std::ostringstream oss;
        oss << "Failed to open WinHTTP session. Error: " << GetLastError();
        SetError(oss.str());
        state_ = WebSocketState::ERROR_STATE;
        return false;
    }
    
    // Connect to server
    h_connection_ = WinHttpConnect(h_session_, host_.c_str(), port_, 0);
    if (!h_connection_) {
        std::ostringstream oss;
        oss << "Failed to connect to server. Error: " << GetLastError();
        SetError(oss.str());
        CleanupHandles();
        state_ = WebSocketState::ERROR_STATE;
        return false;
    }
    
    // Open request
    DWORD flags = WINHTTP_FLAG_SECURE;
    if (!secure_) {
        flags = 0;
    }
    
    HINTERNET h_request = WinHttpOpenRequest(
        h_connection_,
        L"GET",
        path_.c_str(),
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags
    );
    
    if (!h_request) {
        std::ostringstream oss;
        oss << "Failed to open request. Error: " << GetLastError();
        SetError(oss.str());
        CleanupHandles();
        state_ = WebSocketState::ERROR_STATE;
        return false;
    }
    
    // Upgrade to WebSocket
    BOOL result = WinHttpSetOption(
        h_request,
        WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,
        NULL,
        0
    );
    
    if (!result) {
        std::ostringstream oss;
        oss << "Failed to set WebSocket upgrade option. Error: " << GetLastError();
        SetError(oss.str());
        WinHttpCloseHandle(h_request);
        CleanupHandles();
        state_ = WebSocketState::ERROR_STATE;
        return false;
    }
    
    // Send request
    result = WinHttpSendRequest(
        h_request,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0
    );
    
    if (!result) {
        std::ostringstream oss;
        oss << "Failed to send WebSocket upgrade request. Error: " << GetLastError();
        SetError(oss.str());
        WinHttpCloseHandle(h_request);
        CleanupHandles();
        state_ = WebSocketState::ERROR_STATE;
        return false;
    }
    
    // Receive response
    result = WinHttpReceiveResponse(h_request, NULL);
    if (!result) {
        std::ostringstream oss;
        oss << "Failed to receive WebSocket upgrade response. Error: " << GetLastError();
        SetError(oss.str());
        WinHttpCloseHandle(h_request);
        CleanupHandles();
        state_ = WebSocketState::ERROR_STATE;
        return false;
    }
    
    // Complete WebSocket handshake
    h_websocket_ = WinHttpWebSocketCompleteUpgrade(h_request, NULL);
    WinHttpCloseHandle(h_request);
    
    if (!h_websocket_) {
        std::ostringstream oss;
        oss << "Failed to complete WebSocket upgrade. Error: " << GetLastError();
        SetError(oss.str());
        CleanupHandles();
        state_ = WebSocketState::ERROR_STATE;
        return false;
    }
    
    // Connection successful
    state_ = WebSocketState::CONNECTED;
    Logger::Info("WebSocket connected to: " + url);
    
    // Start receive thread
    receive_thread_running_ = true;
    receive_thread_ = std::thread(&WebSocketClient::ReceiveThreadFunction, this);
    
    // Call connected callback
    if (on_connected_) {
        on_connected_();
    }
    
    return true;
}

void WebSocketClient::Disconnect() {
    if (state_ == WebSocketState::DISCONNECTED) {
        return;
    }
    
    state_ = WebSocketState::DISCONNECTING;
    
    // Stop receive thread
    receive_thread_running_ = false;
    
    // Close WebSocket first to unblock the receive thread
    if (h_websocket_) {
        WinHttpWebSocketClose(h_websocket_, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
    }
    
    // Wait for receive thread with timeout
    if (receive_thread_.joinable()) {
        // Give the thread a short time to finish gracefully
        auto start = std::chrono::steady_clock::now();
        bool joined = false;
        
        while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500)) {
            if (!receive_thread_running_) {
                // Thread has finished, safe to join
                receive_thread_.join();
                joined = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        if (!joined) {
            // Thread didn't finish in time, force detach
            Logger::Warning("WebSocket receive thread did not join in time, detaching");
            receive_thread_.detach();
        }
    }
    
    CleanupHandles();
    state_ = WebSocketState::DISCONNECTED;
    
    Logger::Info("WebSocket disconnected");
    
    // Call disconnected callback
    if (on_disconnected_) {
        on_disconnected_("User requested disconnect");
    }
}

bool WebSocketClient::IsConnected() const {
    return state_ == WebSocketState::CONNECTED;
}

bool WebSocketClient::SendText(const std::string& message) {
    if (!IsConnected()) {
        SetError("Cannot send - not connected");
        return false;
    }
    
    DWORD result = WinHttpWebSocketSend(
        h_websocket_,
        WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
        (PVOID)message.c_str(),
        (DWORD)message.length()
    );
    
    if (result != ERROR_SUCCESS) {
        std::ostringstream oss;
        oss << "Failed to send WebSocket message. Error: " << result;
        SetError(oss.str());
        return false;
    }
    
    Logger::Debug("WebSocket sent: " + message);
    return true;
}

bool WebSocketClient::SendBinary(const void* data, size_t length) {
    if (!IsConnected()) {
        SetError("Cannot send - not connected");
        return false;
    }
    
    DWORD result = WinHttpWebSocketSend(
        h_websocket_,
        WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
        (PVOID)data,
        (DWORD)length
    );
    
    if (result != ERROR_SUCCESS) {
        std::ostringstream oss;
        oss << "Failed to send WebSocket binary message. Error: " << result;
        SetError(oss.str());
        return false;
    }
    
    return true;
}

void WebSocketClient::Update() {
    // Process queued messages
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!message_queue_.empty()) {
        std::string message = message_queue_.front();
        message_queue_.pop();
        
        if (on_message_) {
            on_message_(message);
        }
    }
}

void WebSocketClient::ReceiveThreadFunction() {
    Logger::Debug("WebSocket receive thread started");
    
    const DWORD BUFFER_SIZE = 8192;
    std::vector<BYTE> buffer(BUFFER_SIZE);
    std::string accumulated_message;
    
    while (receive_thread_running_ && state_ == WebSocketState::CONNECTED) {
        DWORD bytes_read = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE buffer_type;
        
        DWORD result = WinHttpWebSocketReceive(
            h_websocket_,
            buffer.data(),
            BUFFER_SIZE,
            &bytes_read,
            &buffer_type
        );
        
        if (result != ERROR_SUCCESS) {
            if (receive_thread_running_) {
                std::ostringstream oss;
                oss << "WebSocket receive failed. Error: " << result;
                std::string error_msg = oss.str();
                SetError(error_msg);
                state_ = WebSocketState::ERROR_STATE;
                
                if (on_error_) {
                    on_error_(error_msg);
                }
                if (on_disconnected_) {
                    on_disconnected_("Receive error");
                }
            }
            break;
        }
        
        if (buffer_type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
            buffer_type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) {
            
            // Append to accumulated message
            accumulated_message.append((char*)buffer.data(), bytes_read);
            
            // If this is a complete message (not a fragment), queue it
            if (buffer_type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                message_queue_.push(accumulated_message);
                Logger::Debug("WebSocket received: " + accumulated_message);
                accumulated_message.clear();
            }
        }
        else if (buffer_type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            Logger::Info("WebSocket close frame received");
            receive_thread_running_ = false;
            state_ = WebSocketState::DISCONNECTING;
            
            if (on_disconnected_) {
                on_disconnected_("Server closed connection");
            }
            break;
        }
    }
    
    Logger::Debug("WebSocket receive thread stopped");
}

void WebSocketClient::CleanupHandles() {
    if (h_websocket_) {
        WinHttpCloseHandle(h_websocket_);
        h_websocket_ = nullptr;
    }
    if (h_connection_) {
        WinHttpCloseHandle(h_connection_);
        h_connection_ = nullptr;
    }
    if (h_session_) {
        WinHttpCloseHandle(h_session_);
        h_session_ = nullptr;
    }
}

void WebSocketClient::SetError(const std::string& error) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = error;
    Logger::Error("WebSocketClient: " + error);
}

std::string WebSocketClient::GetLastError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

} // namespace StayPutVR

