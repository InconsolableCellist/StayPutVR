#include "DeviceManager.hpp"
#include "../../../common/Logger.hpp"
#include <thread>
#include <chrono>

namespace StayPutVR {
    bool DeviceManager::Initialize() {
        // Set device update callback first
        ipc_client_.SetDeviceUpdateCallback([this](const std::vector<DevicePositionData>& devices) {
            this->OnDeviceUpdate(devices);
        });
        
        // Try initial connection
        if (ipc_client_.Connect()) {
            if (Logger::IsInitialized()) {
                Logger::Info("DeviceManager: Successfully connected to driver IPC server");
            }
            return true;
        }
        
        // If initial connection fails, start auto-reconnection if enabled
        if (auto_reconnect_enabled_) {
            if (Logger::IsInitialized()) {
                Logger::Warning("DeviceManager: Initial connection failed, starting auto-reconnection");
            }
            StartReconnectThread();
            // Return true to allow application to continue running
            return true;
        } else {
            if (Logger::IsInitialized()) {
                Logger::Error("DeviceManager: Failed to connect to driver IPC server and auto-reconnect is disabled");
            }
            return false;
        }
    }

    void DeviceManager::Shutdown() {
        StopReconnectThread();
        ipc_client_.Disconnect();
    }

    bool DeviceManager::IsConnected() const {
        return ipc_client_.IsConnected();
    }

    void DeviceManager::Update() {
        // Process IPC messages
        ipc_client_.ProcessMessages();
        
        // Check for connection loss and start reconnection if needed
        if (auto_reconnect_enabled_ && !ipc_client_.IsConnected() && !reconnect_thread_running_) {
            if (Logger::IsInitialized()) {
                Logger::Warning("DeviceManager: Connection lost, starting auto-reconnection");
            }
            StartReconnectThread();
        }
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
    
    void DeviceManager::StartReconnectThread() {
        if (reconnect_thread_running_) {
            return; // Already running
        }
        
        // Ensure any previous thread is fully cleaned up
        if (reconnect_thread_.joinable()) {
            reconnect_thread_.join();
        }
        
        reconnect_thread_running_ = true;
        last_reconnect_attempt_ = std::chrono::steady_clock::now();
        reconnect_thread_ = std::thread(&DeviceManager::ReconnectThreadFunction, this);
        
        if (Logger::IsInitialized()) {
            Logger::Info("DeviceManager: Auto-reconnection thread started");
        }
    }
    
    void DeviceManager::StopReconnectThread() {
        if (!reconnect_thread_running_) {
            return; // Not running
        }
        
        reconnect_thread_running_ = false;
        
        if (reconnect_thread_.joinable()) {
            reconnect_thread_.join();
        }
        
        if (Logger::IsInitialized()) {
            Logger::Info("DeviceManager: Auto-reconnection thread stopped");
        }
    }
    
    void DeviceManager::ReconnectThreadFunction() {
        if (Logger::IsInitialized()) {
            Logger::Info("DeviceManager: Reconnection thread started");
        }
        
        // Wait initial delay before first reconnection attempt
        std::this_thread::sleep_for(INITIAL_RECONNECT_DELAY);
        
        while (reconnect_thread_running_ && auto_reconnect_enabled_) {
            if (!ipc_client_.IsConnected()) {
                auto now = std::chrono::steady_clock::now();
                if (now - last_reconnect_attempt_ >= RECONNECT_INTERVAL) {
                    if (TryReconnect()) {
                        if (Logger::IsInitialized()) {
                            Logger::Info("DeviceManager: Successfully reconnected to driver");
                        }
                        break; // Exit thread on successful connection
                    }
                    last_reconnect_attempt_ = now;
                }
            } else {
                // Already connected, exit thread
                break;
            }
            
            // Sleep briefly before next check
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        
        reconnect_thread_running_ = false;
        if (Logger::IsInitialized()) {
            Logger::Info("DeviceManager: Reconnection thread exiting");
        }
    }
    
    bool DeviceManager::TryReconnect() {
        if (Logger::IsInitialized()) {
            Logger::Debug("DeviceManager: Attempting to reconnect to driver...");
        }
        
        // Ensure clean state before reconnecting
        if (ipc_client_.IsConnected()) {
            ipc_client_.Disconnect();
            // Brief delay to ensure cleanup completes
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        return ipc_client_.Connect();
    }
    
    bool DeviceManager::ManualReconnect() {
        if (Logger::IsInitialized()) {
            Logger::Info("DeviceManager: Manual reconnection requested");
        }
        
        // Stop auto-reconnection thread to avoid conflicts
        StopReconnectThread();
        
        // Try to reconnect
        bool success = TryReconnect();
        
        // If manual reconnection fails and auto-reconnect is enabled, restart the thread
        if (!success && auto_reconnect_enabled_) {
            if (Logger::IsInitialized()) {
                Logger::Info("DeviceManager: Manual reconnection failed, restarting auto-reconnection");
            }
            StartReconnectThread();
        }
        
        return success;
    }
}
