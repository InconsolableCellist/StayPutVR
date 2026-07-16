#include "DeviceManager.hpp"
#include "../../../common/Logger.hpp"
#include "../Dataset/DatasetRecorder.hpp"
#include <thread>
#include <chrono>
#include <cmath>

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
        StopSimulation();
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
        // Dataset capture first: this callback runs on the IPC reader thread at
        // the full driver rate (~90 Hz), which is exactly the stream the
        // recorder wants. The UI's GetDevices() poll only sees the latest
        // snapshot, so recording there would silently decimate the data.
        if (DatasetRecorder* recorder = dataset_recorder_.load()) {
            recorder->OnDeviceUpdate(devices);
        }

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
    
    void DeviceManager::StartSimulation() {
        if (simulate_thread_running_) {
            return;
        }
        if (simulate_thread_.joinable()) {
            simulate_thread_.join();
        }
        simulate_thread_running_ = true;
        simulate_thread_ = std::thread(&DeviceManager::SimulateThreadFunction, this);
        if (Logger::IsInitialized()) {
            Logger::Info("DeviceManager: simulated device feed started");
        }
    }

    void DeviceManager::StopSimulation() {
        if (!simulate_thread_running_) {
            return;
        }
        simulate_thread_running_ = false;
        if (simulate_thread_.joinable()) {
            simulate_thread_.join();
        }
        if (Logger::IsInitialized()) {
            Logger::Info("DeviceManager: simulated device feed stopped");
        }
    }

    void DeviceManager::SimulateThreadFunction() {
        // A plausible full-body tracker set moving in slow sinusoids, with an
        // occasional tracking dropout on the hip tracker — deliberately, so the
        // capture pipeline is exercised on the messy cases (dropouts are data).
        struct SimDevice { const char* serial; DeviceType type; float base_y; float phase; };
        const SimDevice sim_devices[] = {
            {"SIM-HMD-001",       DeviceType::HMD,        1.70f, 0.0f},
            {"SIM-CTRL-LEFT",     DeviceType::CONTROLLER, 1.20f, 1.1f},
            {"SIM-CTRL-RIGHT",    DeviceType::CONTROLLER, 1.20f, 2.2f},
            {"SIM-TRACKER-HIP",   DeviceType::TRACKER,    0.95f, 3.3f},
            {"SIM-TRACKER-LFOOT", DeviceType::TRACKER,    0.10f, 4.4f},
            {"SIM-TRACKER-RFOOT", DeviceType::TRACKER,    0.10f, 5.5f},
        };

        const auto start = std::chrono::steady_clock::now();
        while (simulate_thread_running_) {
            const double t = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - start).count();
            const double wall = std::chrono::duration<double>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            const double mono = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();

            std::vector<DevicePositionData> devices;
            devices.reserve(std::size(sim_devices));
            for (const auto& sim : sim_devices) {
                DevicePositionData d;
                d.serial = sim.serial;
                d.type = sim.type;
                const float sway = 0.15f * static_cast<float>(std::sin(t * 0.7 + sim.phase));
                const float bob  = 0.03f * static_cast<float>(std::sin(t * 2.1 + sim.phase));
                d.position[0] = sway;
                d.position[1] = sim.base_y + bob;
                d.position[2] = 0.1f * static_cast<float>(std::cos(t * 0.5 + sim.phase));
                const float yaw = 0.3f * static_cast<float>(std::sin(t * 0.4 + sim.phase));
                d.rotation[0] = 0.0f;
                d.rotation[1] = std::sin(yaw * 0.5f);
                d.rotation[2] = 0.0f;
                d.rotation[3] = std::cos(yaw * 0.5f);
                d.velocity[0] = 0.15f * 0.7f * static_cast<float>(std::cos(t * 0.7 + sim.phase));
                d.velocity[1] = 0.03f * 2.1f * static_cast<float>(std::cos(t * 2.1 + sim.phase));
                d.velocity[2] = -0.1f * 0.5f * static_cast<float>(std::sin(t * 0.5 + sim.phase));
                d.angular_velocity[1] = 0.3f * 0.4f * static_cast<float>(std::cos(t * 0.4 + sim.phase));
                d.connected = true;
                // Hip tracker drops out for ~2 s every 20 s.
                const bool dropout = (sim.type == DeviceType::TRACKER) &&
                                     (sim.phase > 3.0f && sim.phase < 4.0f) &&
                                     (std::fmod(t, 20.0) > 18.0);
                d.pose_valid = !dropout;
                d.tracking_result = dropout ? 201 /*Running_OutOfRange*/ : 200 /*Running_OK*/;
                d.sample_time_wall = wall;
                d.sample_time_mono = mono;
                devices.push_back(std::move(d));
            }

            OnDeviceUpdate(devices);
            std::this_thread::sleep_for(std::chrono::microseconds(11111)); // ~90 Hz
        }
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
