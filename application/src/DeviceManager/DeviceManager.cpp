#include "DeviceManager.hpp"
#include "../../../common/Logger.hpp"

namespace StayPutVR {
    bool DeviceManager::Initialize() {
        // Connect to IPC server
        if (!ipc_client_.Connect()) {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to connect to driver IPC server");
            }
            return false;
        }
        
        // Set device update callback
        ipc_client_.SetDeviceUpdateCallback([this](const std::vector<DevicePositionData>& devices) {
            this->OnDeviceUpdate(devices);
        });
        
        return true;
    }

    void DeviceManager::Shutdown() {
        ipc_client_.Disconnect();
    }

    bool DeviceManager::IsConnected() const {
        return ipc_client_.IsConnected();
    }

    void DeviceManager::Update() {
        // Process IPC messages
        ipc_client_.ProcessMessages();
    }

    const std::vector<DevicePositionData>& DeviceManager::GetDevices() const {
        return devices_;
    }

    bool DeviceManager::LockDevice(const std::string& serial, bool lock) {
        // Send lock command to server
        ipc_client_.SendCommand("lock_device", serial + ":" + (lock ? "true" : "false"));
        return true;
    }

    void DeviceManager::OnDeviceUpdate(const std::vector<DevicePositionData>& devices) {
        // Update local device cache
        devices_ = devices;
        
        // Update device map
        device_map_.clear();
        for (size_t i = 0; i < devices_.size(); ++i) {
            device_map_[devices_[i].serial] = i;
        }
    }
}
