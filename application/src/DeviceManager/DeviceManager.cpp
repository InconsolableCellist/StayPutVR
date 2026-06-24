#include "DeviceManager.hpp"
#include "../../../common/Logger.hpp"
#include <thread>
#include <chrono>

namespace StayPutVR {
    bool DeviceManager::Initialize() {
        ipc_client_.SetDeviceUpdateCallback([this](const std::vector<DevicePositionData>& devices) {
            this->OnDeviceUpdate(devices);
        });
        
        // Connect on the background reconnect thread rather than synchronously:
        // a missing or busy driver pipe used to block here (up to 15s on
        // WaitNamedPipe), freezing application/UI startup. The reconnect thread
        // performs the (possibly blocking) attempts off the main thread.
        if (auto_reconnect_enabled_) {
            if (Logger::IsInitialized()) {
                Logger::Info("DeviceManager: Connecting to driver in background");
            }
            StartReconnectThread();
            return true;
        }

        // Auto-reconnect disabled: caller wants a single synchronous attempt.
        if (ipc_client_.Connect()) {
            if (Logger::IsInitialized()) {
                Logger::Info("DeviceManager: Successfully connected to driver IPC server");
            }
            return true;
        }
        if (Logger::IsInitialized()) {
            Logger::Error("DeviceManager: Failed to connect to driver IPC server and auto-reconnect is disabled");
        }
        return false;
    }

    void DeviceManager::Shutdown() {
        StopReconnectThread();
        ipc_client_.Disconnect();
    }

    bool DeviceManager::IsConnected() const {
        return ipc_client_.IsConnected();
    }

    void DeviceManager::Update() {
        ipc_client_.ProcessMessages();
        
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
        reconnect_attempts_ = 0;
        // Backdate so the very first attempt fires right after INITIAL_RECONNECT_DELAY
        // instead of waiting a full RECONNECT_INTERVAL on top of it.
        last_reconnect_attempt_ = std::chrono::steady_clock::now() - RECONNECT_INTERVAL;
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
        int attempt = ++reconnect_attempts_;
        if (Logger::IsInitialized()) {
            Logger::Debug("DeviceManager: Attempting to connect to driver (attempt " +
                std::to_string(attempt) + ")...");
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
