#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <chrono>
#include "../../../common/DeviceTypes.hpp"
#include "../IPC/IPCClient.hpp"

namespace StayPutVR {
    class DeviceManager {
    public:
        bool Initialize();
        void Shutdown();
        bool IsConnected() const;
        void Update(); // Process IPC messages
        
        // Device management
        const std::vector<DevicePositionData>& GetDevices() const;
        bool LockDevice(const std::string& serial, bool lock);
        
        // Auto-reconnection control
        void SetAutoReconnect(bool enabled) { auto_reconnect_enabled_ = enabled; }
        bool IsAutoReconnectEnabled() const { return auto_reconnect_enabled_; }
        
        // Manual reconnection (thread-safe, stops auto-reconnection temporarily)
        bool ManualReconnect();

        // Connection status for the UI. True while the background reconnect
        // thread is actively trying to reach the driver.
        bool IsReconnecting() const { return reconnect_thread_running_.load(); }
        // Number of connection attempts made by the current reconnect session.
        int GetReconnectAttempts() const { return reconnect_attempts_.load(); }

    private:
        IPCClient ipc_client_;
        std::vector<DevicePositionData> devices_;
        std::unordered_map<std::string, size_t> device_map_; // serial to index
        
        // Auto-reconnection
        std::atomic<bool> auto_reconnect_enabled_ = true;
        std::atomic<bool> reconnect_thread_running_ = false;
        std::atomic<int> reconnect_attempts_ = 0;
        std::thread reconnect_thread_;
        std::chrono::steady_clock::time_point last_reconnect_attempt_;
        static constexpr std::chrono::seconds RECONNECT_INTERVAL{5};
        static constexpr std::chrono::seconds INITIAL_RECONNECT_DELAY{2};
        
        void OnDeviceUpdate(const std::vector<DevicePositionData>& devices);
        void ReconnectThreadFunction();
        void StartReconnectThread();
        void StopReconnectThread();
        bool TryReconnect();
    };
}
