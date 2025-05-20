#include "OSCManager.hpp"
#include "Logger.hpp"
#include <sstream>

namespace StayPutVR {

OSCManager& OSCManager::GetInstance() {
    static OSCManager instance;
    return instance;
}

OSCManager::~OSCManager() {
    if (initialized_) {
        Shutdown();
    }
}

bool OSCManager::Initialize(const std::string& address, int send_port, int receive_port) {
    if (initialized_) {
        if (Logger::IsInitialized()) {
            Logger::Debug("OSCManager: Already initialized with address " + address_ + 
                          " (send port: " + std::to_string(send_port_) + 
                          ", receive port: " + std::to_string(receive_port_) + ")");
        }
        return true;
    }

    address_ = address;
    send_port_ = send_port;
    receive_port_ = receive_port;

    // Initialize Winsock
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        if (Logger::IsInitialized()) {
            Logger::Error("OSCManager: WSAStartup failed with error: " + std::to_string(result));
        }
        return false;
    }

    // Create UDP socket for sending
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET) {
        if (Logger::IsInitialized()) {
            Logger::Error("OSCManager: Socket creation failed with error: " + std::to_string(WSAGetLastError()));
        }
        WSACleanup();
        return false;
    }

    // Allocate and set up the server address structure for sending
    server_addr_ = new sockaddr_in();
    ZeroMemory(server_addr_, sizeof(sockaddr_in));
    server_addr_->sin_family = AF_INET;
    server_addr_->sin_port = htons(static_cast<u_short>(send_port));
    
    // Convert IPv4 address from text to binary form
    result = inet_pton(AF_INET, address.c_str(), &(server_addr_->sin_addr));
    if (result != 1) {
        if (Logger::IsInitialized()) {
            Logger::Error("OSCManager: inet_pton failed with error: " + std::to_string(WSAGetLastError()));
        }
        closesocket(socket_);
        delete server_addr_;
        server_addr_ = nullptr;
        WSACleanup();
        return false;
    }
    
    // Create UDP socket for receiving
    receive_socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (receive_socket_ == INVALID_SOCKET) {
        if (Logger::IsInitialized()) {
            Logger::Error("OSCManager: Receive socket creation failed with error: " + std::to_string(WSAGetLastError()));
        }
        closesocket(socket_);
        delete server_addr_;
        server_addr_ = nullptr;
        WSACleanup();
        return false;
    }
    
    // Set up the local address structure for receiving
    sockaddr_in local_addr;
    ZeroMemory(&local_addr, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(static_cast<u_short>(receive_port));
    
    // Bind the receive socket
    result = bind(receive_socket_, (sockaddr*)&local_addr, sizeof(local_addr));
    if (result == SOCKET_ERROR) {
        if (Logger::IsInitialized()) {
            Logger::Error("OSCManager: bind failed with error: " + std::to_string(WSAGetLastError()));
        }
        closesocket(socket_);
        closesocket(receive_socket_);
        delete server_addr_;
        server_addr_ = nullptr;
        WSACleanup();
        return false;
    }
    
    // Start the receive thread
    receive_thread_running_ = true;
    try {
        receive_thread_ = std::thread(&OSCManager::ReceiveThreadFunction, this);
        
        if (Logger::IsInitialized()) {
            Logger::Info("OSCManager: Receive thread started on port " + std::to_string(receive_port));
        }
    }
    catch (const std::exception& e) {
        if (Logger::IsInitialized()) {
            Logger::Error("OSCManager: Failed to start receive thread: " + std::string(e.what()));
        }
        receive_thread_running_ = false;
        closesocket(socket_);
        closesocket(receive_socket_);
        delete server_addr_;
        server_addr_ = nullptr;
        WSACleanup();
        return false;
    }
    
    initialized_ = true;

    if (Logger::IsInitialized()) {
        Logger::Info("OSCManager: Initialized with address " + address + 
                    " (send port: " + std::to_string(send_port) + 
                    ", receive port: " + std::to_string(receive_port) + ")");
    }

    return true;
}

void OSCManager::Shutdown() {
    if (!initialized_) {
        if (Logger::IsInitialized()) {
            Logger::Debug("OSCManager: Shutdown called but not initialized");
        }
        return;
    }
    
    // Stop the receive thread
    receive_thread_running_ = false;
    
    // Wait for the thread to finish
    if (receive_thread_.joinable()) {
        if (Logger::IsInitialized()) {
            Logger::Debug("OSCManager: Waiting for receive thread to finish...");
        }
        receive_thread_.join();
        if (Logger::IsInitialized()) {
            Logger::Debug("OSCManager: Receive thread stopped");
        }
    }

    // Clean up sockets
    closesocket(socket_);
    closesocket(receive_socket_);
    
    // Clean up Winsock
    WSACleanup();
    
    // Free server address structure
    delete server_addr_;
    server_addr_ = nullptr;
    
    initialized_ = false;

    if (Logger::IsInitialized()) {
        Logger::Info("OSCManager: Shutdown");
        Logger::Debug("OSCManager: OSC client connection closed");
    }
}

void OSCManager::SetConfig(const Config& config) {
    // Update the OSC paths from config
    osc_lock_path_hmd_ = config.osc_lock_path_hmd;
    osc_lock_path_left_hand_ = config.osc_lock_path_left_hand;
    osc_lock_path_right_hand_ = config.osc_lock_path_right_hand;
    osc_lock_path_left_foot_ = config.osc_lock_path_left_foot;
    osc_lock_path_right_foot_ = config.osc_lock_path_right_foot;
    osc_lock_path_hip_ = config.osc_lock_path_hip;
    osc_global_lock_path_ = config.osc_global_lock_path;
    osc_global_unlock_path_ = config.osc_global_unlock_path;
    
    if (Logger::IsInitialized()) {
        Logger::Debug("OSCManager: Updated OSC paths from config");
    }
}

void OSCManager::ReceiveThreadFunction() {
    if (Logger::IsInitialized()) {
        Logger::Debug("OSCManager: Receive thread started");
    }
    
    // Buffer for incoming data
    std::array<char, MAX_PACKET_SIZE> recv_buffer;
    
    // Set up timeout for recv to allow checking the running flag
    DWORD timeout = 500; // 500 ms
    setsockopt(receive_socket_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    
    while (receive_thread_running_) {
        // Receive data
        int bytes_received = recv(receive_socket_, recv_buffer.data(), recv_buffer.size(), 0);
        
        if (bytes_received == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error == WSAETIMEDOUT) {
                // Timeout, just continue to check the running flag
                continue;
            }
            else if (error == WSAEMSGSIZE) {
                // Message too large for the buffer
                if (Logger::IsInitialized()) {
                    Logger::Warning("OSCManager: Received an OSC message that exceeds the buffer size (" + 
                                   std::to_string(MAX_PACKET_SIZE) + " bytes). Consider increasing MAX_PACKET_SIZE.");
                }
                // Continue processing despite this error
                continue;
            }
            else {
                if (Logger::IsInitialized()) {
                    Logger::Error("OSCManager: recv failed with error: " + std::to_string(error));
                }
                // Continue running despite errors
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
        }
        
        if (bytes_received > 0) {
            // Process the received OSC message
            ProcessOSCMessage(recv_buffer.data(), bytes_received);
        }
    }
    
    if (Logger::IsInitialized()) {
        Logger::Debug("OSCManager: Receive thread stopped");
    }
}

void OSCManager::ProcessOSCMessage(const char* data, size_t size) {
    if (!lock_callback_ && !global_lock_callback_) {
        // No callbacks registered
        if (Logger::IsInitialized()) {
            Logger::Debug("OSCManager: No callbacks registered, skipping message");
        }
        return;
    }
    
    try {
        // Parse OSC packet
        OSCPP::Server::Packet packet(data, size);
        
        // Check if it's a message
        if (packet.isMessage()) {
            OSCPP::Server::Message message(packet);
            std::string address = message.address();
            
            // Only log messages that match the SPVR pattern
            bool shouldLog = address.find("/avatar/parameters/SPVR_") == 0;
            
            if (Logger::IsInitialized() && shouldLog) {
                Logger::Debug("OSCManager: Received OSC message: " + address);
            }
            
            // Get the argument stream from the message
            OSCPP::Server::ArgStream args = message.args();
            
            // Check if we have at least one argument
            if (!args.atEnd()) {
                bool value_bool = false;
                char tag = args.tag(); // Get the type of the next argument
                
                if (tag == 'f') {
                    float value = args.float32();
                    value_bool = value > 0.5f; // Consider values > 0.5 as true
                    if (Logger::IsInitialized() && shouldLog) {
                        Logger::Debug("OSCManager: OSC value (float): " + std::to_string(value) + ", bool: " + (value_bool ? "true" : "false"));
                    }
                }
                else if (tag == 'i') {
                    int32_t value = args.int32();
                    value_bool = value != 0; // Any non-zero value is true
                    if (Logger::IsInitialized() && shouldLog) {
                        Logger::Debug("OSCManager: OSC value (int): " + std::to_string(value) + ", bool: " + (value_bool ? "true" : "false"));
                    }
                }
                else if (tag == 'T' || tag == 'F') {
                    // Handle OSC boolean values (T = true, F = false)
                    value_bool = (tag == 'T');
                    if (Logger::IsInitialized() && shouldLog) {
                        Logger::Debug("OSCManager: OSC value (bool): " + std::string(value_bool ? "true" : "false"));
                    }
                }
                else {
                    // Unsupported type
                    if (Logger::IsInitialized() && shouldLog) {
                        Logger::Debug("OSCManager: OSC message has unsupported argument type: " + std::string(1, tag));
                    }
                    return;
                }
                
                // Check if the address matches any of our device lock paths
                // Only trigger callbacks on true values
                if (value_bool) {
                    // Check for global lock/unlock
                    if (address == osc_global_lock_path_) {
                        if (global_lock_callback_) {
                            Logger::Info("OSCManager: Triggering global lock from OSC message");
                            global_lock_callback_(true);
                        }
                    }
                    else if (address == osc_global_unlock_path_) {
                        if (global_lock_callback_) {
                            Logger::Info("OSCManager: Triggering global unlock from OSC message");
                            global_lock_callback_(false);
                        }
                    }
                    // Check for individual device locks
                    else if (address == osc_lock_path_hmd_) {
                        if (lock_callback_) {
                            Logger::Info("OSCManager: Triggering HMD lock from OSC message");
                            lock_callback_(OSCDeviceType::HMD, true);
                        }
                    }
                    else if (address == osc_lock_path_left_hand_ || address == "/avatar/parameters/SPVR_handLeft_enter") {
                        if (lock_callback_) {
                            Logger::Info("OSCManager: Triggering left hand lock from OSC message");
                            lock_callback_(OSCDeviceType::ControllerLeft, true);
                        }
                    }
                    else if (address == osc_lock_path_right_hand_ || address == "/avatar/parameters/SPVR_handRight_enter") {
                        if (lock_callback_) {
                            Logger::Info("OSCManager: Triggering right hand lock from OSC message");
                            lock_callback_(OSCDeviceType::ControllerRight, true);
                        }
                    }
                    else if (address == osc_lock_path_left_foot_ || address == "/avatar/parameters/SPVR_footLeft_enter") {
                        if (lock_callback_) {
                            Logger::Info("OSCManager: Triggering left foot lock from OSC message");
                            lock_callback_(OSCDeviceType::FootLeft, true);
                        }
                    }
                    else if (address == osc_lock_path_right_foot_ || address == "/avatar/parameters/SPVR_footRight_enter") {
                        if (lock_callback_) {
                            Logger::Info("OSCManager: Triggering right foot lock from OSC message");
                            lock_callback_(OSCDeviceType::FootRight, true);
                        }
                    }
                    else if (address == osc_lock_path_hip_ || address == "/avatar/parameters/SPVR_hip_enter") {
                        if (lock_callback_) {
                            Logger::Info("OSCManager: Triggering hip lock from OSC message");
                            lock_callback_(OSCDeviceType::Hip, true);
                        }
                    }
                    else if (address == "/avatar/parameters/SPVR_head_enter") {
                        if (lock_callback_) {
                            Logger::Info("OSCManager: Triggering HMD lock from OSC message (alternate path)");
                            lock_callback_(OSCDeviceType::HMD, true);
                        }
                    }
                }
                else {
                    // For false values, handle unlocking (we only care about the individual device paths)
                    if (address == osc_lock_path_hmd_) {
                        if (lock_callback_) {
                            Logger::Info("OSCManager: Triggering HMD unlock from OSC message");
                            lock_callback_(OSCDeviceType::HMD, false);
                        }
                    }
                    else if (address == osc_lock_path_left_hand_ || address == "/avatar/parameters/SPVR_handLeft_enter") {
                        if (lock_callback_) {
                            Logger::Info("OSCManager: Triggering left hand unlock from OSC message");
                            lock_callback_(OSCDeviceType::ControllerLeft, false);
                        }
                    }
                    else if (address == osc_lock_path_right_hand_ || address == "/avatar/parameters/SPVR_handRight_enter") {
                        if (lock_callback_) {
                            Logger::Info("OSCManager: Triggering right hand unlock from OSC message");
                            lock_callback_(OSCDeviceType::ControllerRight, false);
                        }
                    }
                    else if (address == osc_lock_path_left_foot_ || address == "/avatar/parameters/SPVR_footLeft_enter") {
                        if (lock_callback_) {
                            Logger::Info("OSCManager: Triggering left foot unlock from OSC message");
                            lock_callback_(OSCDeviceType::FootLeft, false);
                        }
                    }
                    else if (address == osc_lock_path_right_foot_ || address == "/avatar/parameters/SPVR_footRight_enter") {
                        if (lock_callback_) {
                            Logger::Info("OSCManager: Triggering right foot unlock from OSC message");
                            lock_callback_(OSCDeviceType::FootRight, false);
                        }
                    }
                    else if (address == osc_lock_path_hip_ || address == "/avatar/parameters/SPVR_hip_enter") {
                        if (lock_callback_) {
                            Logger::Info("OSCManager: Triggering hip unlock from OSC message");
                            lock_callback_(OSCDeviceType::Hip, false);
                        }
                    }
                    else if (address == "/avatar/parameters/SPVR_head_enter") {
                        if (lock_callback_) {
                            Logger::Info("OSCManager: Triggering HMD unlock from OSC message (alternate path)");
                            lock_callback_(OSCDeviceType::HMD, false);
                        }
                    }
                }
            }
        }
    }
    catch (const std::exception& e) {
        if (Logger::IsInitialized()) {
            Logger::Error("OSCManager: Error processing OSC message: " + std::string(e.what()));
        }
    }
}

void OSCManager::SendDeviceStatus(OSCDeviceType device, DeviceStatus status) {
    if (!initialized_) {
        if (Logger::IsInitialized()) {
            Logger::Debug("OSCManager: Cannot send device status, OSC is not initialized");
        }
        return;
    }

    std::string path = GetStatusPath(device);
    int statusValue = static_cast<int>(status);

    if (SendOSCMessage(path, statusValue) && Logger::IsInitialized()) {
        Logger::Debug("OSCManager: Sending status " + std::to_string(statusValue) + " to " + path + 
            " (device=" + GetDeviceString(device) + ", status=" + std::to_string(statusValue) + ")");
    }
}

void OSCManager::SendPiShockGroup(int group) {
    if (!initialized_) {
        if (Logger::IsInitialized()) {
            Logger::Debug("OSCManager: Cannot send PiShock Group, OSC is not initialized");
        }
        return;
    }
    
    std::string path = "/VRCOSC/PiShock/Group";
    
    if (SendOSCMessage(path, group) && Logger::IsInitialized()) {
        Logger::Debug("OSCManager: Sending PiShock Group " + std::to_string(group) + " to " + path + 
            " (int value=" + std::to_string(group) + ")");
    }
}

void OSCManager::SendPiShockDuration(float duration) {
    if (!initialized_) {
        if (Logger::IsInitialized()) {
            Logger::Debug("OSCManager: Cannot send PiShock Duration, OSC is not initialized");
        }
        return;
    }
    
    // Clamp duration to 0-1 range
    duration = (std::max)(0.0f, (std::min)(duration, 1.0f));
    
    std::string path = "/VRCOSC/PiShock/Duration";
    
    if (SendOSCMessage(path, duration) && Logger::IsInitialized()) {
        Logger::Debug("OSCManager: Sending PiShock Duration " + std::to_string(duration) + " to " + path + 
            " (float value=" + std::to_string(duration) + ", clamped to 0-1 range)");
    }
}

void OSCManager::SendPiShockIntensity(float intensity) {
    if (!initialized_) {
        if (Logger::IsInitialized()) {
            Logger::Debug("OSCManager: Cannot send PiShock Intensity, OSC is not initialized");
        }
        return;
    }
    
    // Clamp intensity to 0-1 range
    intensity = (std::max)(0.0f, (std::min)(intensity, 1.0f));
    
    std::string path = "/VRCOSC/PiShock/Intensity";
    
    if (SendOSCMessage(path, intensity) && Logger::IsInitialized()) {
        Logger::Debug("OSCManager: Sending PiShock Intensity " + std::to_string(intensity) + " to " + path + 
            " (float value=" + std::to_string(intensity) + ", clamped to 0-1 range)");
    }
}

void OSCManager::SendPiShockShock(bool enabled) {
    if (!initialized_) {
        if (Logger::IsInitialized()) {
            Logger::Debug("OSCManager: Cannot send PiShock Shock, OSC is not initialized");
        }
        return;
    }
    
    std::string path = "/VRCOSC/PiShock/Shock";
    
    if (SendOSCMessage(path, enabled) && Logger::IsInitialized()) {
        Logger::Debug("OSCManager: Sending PiShock Shock " + std::string(enabled ? "true" : "false") + " to " + path + 
            " (bool value=" + std::string(enabled ? "true" : "false") + ")");
    }
}

void OSCManager::SendPiShockVibrate(bool enabled) {
    if (!initialized_) {
        if (Logger::IsInitialized()) {
            Logger::Debug("OSCManager: Cannot send PiShock Vibrate, OSC is not initialized");
        }
        return;
    }
    
    std::string path = "/VRCOSC/PiShock/Vibrate";
    
    if (SendOSCMessage(path, enabled) && Logger::IsInitialized()) {
        Logger::Debug("OSCManager: Sending PiShock Vibrate " + std::string(enabled ? "true" : "false") + " to " + path + 
            " (bool value=" + std::string(enabled ? "true" : "false") + ")");
    }
}

void OSCManager::SendPiShockBeep(bool enabled) {
    if (!initialized_) {
        if (Logger::IsInitialized()) {
            Logger::Debug("OSCManager: Cannot send PiShock Beep, OSC is not initialized");
        }
        return;
    }
    
    std::string path = "/VRCOSC/PiShock/Beep";
    
    if (SendOSCMessage(path, enabled) && Logger::IsInitialized()) {
        Logger::Debug("OSCManager: Sending PiShock Beep " + std::string(enabled ? "true" : "false") + " to " + path + 
            " (bool value=" + std::string(enabled ? "true" : "false") + ")");
    }
}

bool OSCManager::SendOSCMessage(const std::string& path, int value) {
    try {
        // Create OSC packet with int32 message
        OSCPP::Client::Packet packet(buffer_.data(), buffer_.size());
        packet.openMessage(path.c_str(), 1)
              .int32(value)
              .closeMessage();

        // Send the packet
        int result = sendto(socket_, 
                     static_cast<const char*>(packet.data()), 
                     static_cast<int>(packet.size()), 
                     0, 
                     reinterpret_cast<struct sockaddr*>(server_addr_), 
                     sizeof(sockaddr_in));
                     
        if (result == SOCKET_ERROR) {
            if (Logger::IsInitialized()) {
                Logger::Error("OSCManager: Failed to send OSC message, error: " + std::to_string(WSAGetLastError()));
            }
            return false;
        }
        
        return true;
    }
    catch (const std::exception& e) {
        if (Logger::IsInitialized()) {
            Logger::Error("OSCManager: Exception when sending OSC message: " + std::string(e.what()));
        }
        return false;
    }
}

bool OSCManager::SendOSCMessage(const std::string& path, float value) {
    try {
        // Create OSC packet with float message
        OSCPP::Client::Packet packet(buffer_.data(), buffer_.size());
        packet.openMessage(path.c_str(), 1)
              .float32(value)
              .closeMessage();

        // Send the packet
        int result = sendto(socket_, 
                     static_cast<const char*>(packet.data()), 
                     static_cast<int>(packet.size()), 
                     0, 
                     reinterpret_cast<struct sockaddr*>(server_addr_), 
                     sizeof(sockaddr_in));
                     
        if (result == SOCKET_ERROR) {
            if (Logger::IsInitialized()) {
                Logger::Error("OSCManager: Failed to send OSC message, error: " + std::to_string(WSAGetLastError()));
            }
            return false;
        }
        
        return true;
    }
    catch (const std::exception& e) {
        if (Logger::IsInitialized()) {
            Logger::Error("OSCManager: Exception when sending OSC message: " + std::string(e.what()));
        }
        return false;
    }
}

bool OSCManager::SendOSCMessage(const std::string& path, bool value) {
    // OSC doesn't have a native bool type, so we send as int (1 for true, 0 for false)
    return SendOSCMessage(path, value ? 1 : 0);
}

std::string OSCManager::GetDeviceString(OSCDeviceType device) const {
    switch (device) {
        case OSCDeviceType::HMD: return "HMD";
        case OSCDeviceType::ControllerLeft: return "ControllerLeft";
        case OSCDeviceType::ControllerRight: return "ControllerRight";
        case OSCDeviceType::FootLeft: return "FootLeft";
        case OSCDeviceType::FootRight: return "FootRight";
        case OSCDeviceType::Hip: return "Hip";
        default: return "Unknown";
    }
}

std::string OSCManager::GetStatusPath(OSCDeviceType device) const {
    return "/avatar/parameters/SPVR_" + GetDeviceString(device) + "_Status";
}

std::string OSCManager::GetLockPath(OSCDeviceType device) const {
    return "/avatar/parameters/SPVR_" + GetDeviceString(device) + "_OnEnter";
}

} // namespace StayPutVR 