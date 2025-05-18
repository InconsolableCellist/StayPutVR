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

bool OSCManager::Initialize(const std::string& address, int port) {
    if (initialized_) {
        if (Logger::IsInitialized()) {
            Logger::Debug("OSCManager: Already initialized with address " + address_ + ":" + std::to_string(port_));
        }
        return true;
    }

    address_ = address;
    port_ = port;

    // Initialize Winsock
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        if (Logger::IsInitialized()) {
            Logger::Error("OSCManager: WSAStartup failed with error: " + std::to_string(result));
        }
        return false;
    }

    // Create UDP socket
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET) {
        if (Logger::IsInitialized()) {
            Logger::Error("OSCManager: Socket creation failed with error: " + std::to_string(WSAGetLastError()));
        }
        WSACleanup();
        return false;
    }

    // Allocate and set up the server address structure
    server_addr_ = new sockaddr_in();
    ZeroMemory(server_addr_, sizeof(sockaddr_in));
    server_addr_->sin_family = AF_INET;
    server_addr_->sin_port = htons(static_cast<u_short>(port));
    
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
    
    initialized_ = true;

    if (Logger::IsInitialized()) {
        Logger::Info("OSCManager: Initialized with address " + address + ":" + std::to_string(port));
        Logger::Debug("OSCManager: OSC client initialization details - address=" + address + ", port=" + std::to_string(port));
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

    // Clean up socket
    closesocket(socket_);
    
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
    duration = std::max(0.0f, std::min(duration, 1.0f));
    
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
    intensity = std::max(0.0f, std::min(intensity, 1.0f));
    
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