#pragma once
#include <string>
#include <vector>
#include <unordered_map>
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
        
    private:
        IPCClient ipc_client_;
        std::vector<DevicePositionData> devices_;
        std::unordered_map<std::string, size_t> device_map_; // serial to index
        
        void OnDeviceUpdate(const std::vector<DevicePositionData>& devices);
    };
}
