#pragma once

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <array>
#include <thread>
#include <atomic>
#include "DeviceTypes.hpp"
#include "Config.hpp"

// OSC headers - included directly
#include <oscpp/client.hpp>
#include <oscpp/server.hpp>

namespace StayPutVR {

// Maximum OSC packet size
constexpr size_t MAX_PACKET_SIZE = 8192;

enum class OSCDeviceType {
    HMD,
    ControllerLeft,
    ControllerRight,
    FootLeft,
    FootRight,
    Hip
};

enum class DeviceStatus {
    Disabled = 0,
    Unlocked = 1,
    LockedSafe = 2,
    LockedWarning = 3,
    LockedDisobedience = 4,
    LockedOutOfBounds = 5
};

class OSCManager {
public:
    static OSCManager& GetInstance();

    bool Initialize(const std::string& address, int send_port, int receive_port);
    void Shutdown();
    bool IsInitialized() const { return initialized_; }
    
    // Function to set config for device lock paths
    void SetConfig(const Config& config);

    // Send device status updates
    void SendDeviceStatus(OSCDeviceType device, DeviceStatus status);
    
    // Set callback for when a device should be locked/unlocked
    void SetLockCallback(std::function<void(OSCDeviceType, bool)> callback) { lock_callback_ = callback; }
    
    // Set callback for global lock/unlock
    void SetGlobalLockCallback(std::function<void(bool)> callback) { global_lock_callback_ = callback; }
    
    // VRCOSC PiShock methods
    void SendPiShockGroup(int group);
    void SendPiShockDuration(float duration); // 0-1 float
    void SendPiShockIntensity(float intensity); // 0-1 float
    void SendPiShockShock(bool enabled);
    void SendPiShockVibrate(bool enabled);
    void SendPiShockBeep(bool enabled);

private:
    OSCManager() = default;
    ~OSCManager();
    OSCManager(const OSCManager&) = delete;
    OSCManager& operator=(const OSCManager&) = delete;

    bool initialized_ = false;
    std::string address_;
    int send_port_ = 9000;
    int receive_port_ = 9005;
    
    // Socket and buffer
    SOCKET socket_ = INVALID_SOCKET;
    SOCKET receive_socket_ = INVALID_SOCKET;
    sockaddr_in* server_addr_ = nullptr;
    std::array<char, MAX_PACKET_SIZE> buffer_;
    
    // Thread for receiving OSC messages
    std::thread receive_thread_;
    std::atomic<bool> receive_thread_running_ = false;
    
    // Receive thread function
    void ReceiveThreadFunction();
    
    // Process received OSC message
    void ProcessOSCMessage(const char* data, size_t size);
    
    // OSC paths from config
    std::string osc_lock_path_hmd_ = "/avatar/parameters/SPVR_neck_enter";
    std::string osc_lock_path_left_hand_ = "/avatar/parameters/SPVR_handLeft_enter";
    std::string osc_lock_path_right_hand_ = "/avatar/parameters/SPVR_handRight_enter";
    std::string osc_lock_path_left_foot_ = "/avatar/parameters/SPVR_footLeft_enter";
    std::string osc_lock_path_right_foot_ = "/avatar/parameters/SPVR_footRight_enter";
    std::string osc_lock_path_hip_ = "/avatar/parameters/SPVR_hip_enter";
    std::string osc_global_lock_path_ = "/avatar/parameters/SPVR_global_lock";
    std::string osc_global_unlock_path_ = "/avatar/parameters/SPVR_global_unlock";
    
    // Helper methods for sending OSC messages
    bool SendOSCMessage(const std::string& path, int value);
    bool SendOSCMessage(const std::string& path, float value);
    bool SendOSCMessage(const std::string& path, bool value);
    
    // Callback for lock/unlock events
    std::function<void(OSCDeviceType, bool)> lock_callback_;
    
    // Callback for global lock/unlock events
    std::function<void(bool)> global_lock_callback_;

    // Helper functions
    std::string GetDeviceString(OSCDeviceType device) const;
    std::string GetStatusPath(OSCDeviceType device) const;
    std::string GetLockPath(OSCDeviceType device) const;
};

} // namespace StayPutVR 