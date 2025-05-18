#pragma once

// Include WinSock2.h before any possible Windows.h inclusion
#include <WinSock2.h>
#include <WS2tcpip.h>

// Now include Windows configuration
#include "WindowsConfig.hpp"

// Standard includes
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <array>
#include "DeviceTypes.hpp"

// OSC headers - included directly
#include <oscpp/client.hpp>

namespace StayPutVR {

// Maximum OSC packet size
constexpr size_t MAX_PACKET_SIZE = 1024;

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

    bool Initialize(const std::string& address, int port);
    void Shutdown();
    bool IsInitialized() const { return initialized_; }

    // Send device status updates
    void SendDeviceStatus(OSCDeviceType device, DeviceStatus status);
    
    // Set callback for when a device should be locked/unlocked
    void SetLockCallback(std::function<void(OSCDeviceType, bool)> callback) { lock_callback_ = callback; }
    
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
    int port_ = 9000;
    
    // Socket and buffer
    SOCKET socket_ = INVALID_SOCKET;
    sockaddr_in* server_addr_ = nullptr;
    std::array<char, MAX_PACKET_SIZE> buffer_;
    
    // Helper methods for sending OSC messages
    bool SendOSCMessage(const std::string& path, int value);
    bool SendOSCMessage(const std::string& path, float value);
    bool SendOSCMessage(const std::string& path, bool value);
    
    // Callback for lock/unlock events
    std::function<void(OSCDeviceType, bool)> lock_callback_;

    // Helper functions
    std::string GetDeviceString(OSCDeviceType device) const;
    std::string GetStatusPath(OSCDeviceType device) const;
    std::string GetLockPath(OSCDeviceType device) const;
};

} // namespace StayPutVR 