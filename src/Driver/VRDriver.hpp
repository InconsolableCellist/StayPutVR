#pragma once

#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <string>

#include <openvr_driver.h>

#include "IVRDriver.hpp"
#include "IVRDevice.hpp"

namespace StayPutVR {
    // Global variable for communicating between driver and UI
    extern std::atomic<bool> g_running;
    
    // Structure to represent tracked device info
    struct TrackedDeviceInfo {
        std::string serial;
        DeviceType type;
        vr::TrackedDeviceIndex_t device_index;
        vr::DriverPose_t pose;
    };

    class VRDriver : public IVRDriver {
    public:
        VRDriver();
        
        // Inherited via IVRDriver
        virtual std::vector<std::shared_ptr<IVRDevice>> GetDevices() override;
        virtual std::vector<vr::VREvent_t> GetOpenVREvents() override;
        virtual std::chrono::milliseconds GetLastFrameTime() override;
        virtual bool AddDevice(std::shared_ptr<IVRDevice> device) override;
        virtual SettingsValue GetSettingsValue(std::string key) override;
        virtual void Log(std::string message) override;

        virtual vr::IVRDriverInput* GetInput() override;
        virtual vr::CVRPropertyHelpers* GetProperties() override;
        virtual vr::IVRServerDriverHost* GetDriverHost() override;

        // Inherited via IServerTrackedDeviceProvider
        virtual vr::EVRInitError Init(vr::IVRDriverContext* pDriverContext) override;
        virtual void Cleanup() override;
        virtual void RunFrame() override;
        virtual bool ShouldBlockStandbyMode() override;
        virtual void EnterStandby() override;
        virtual void LeaveStandby() override;
        virtual ~VRDriver();
        
        // Get information about all tracked devices in the system
        std::vector<TrackedDeviceInfo> GetAllTrackedDeviceInfo();

    private:
        std::vector<std::shared_ptr<IVRDevice>> devices_;
        std::vector<vr::VREvent_t> openvr_events_;
        std::chrono::milliseconds frame_timing_ = std::chrono::milliseconds(16);
        std::chrono::system_clock::time_point last_frame_time_ = std::chrono::system_clock::now();
        std::string settings_key_ = "driver_stayputvr";
        
        // UI thread
        std::thread ui_thread_;
        void StartUIThread();
        
        // Device type mapping helper
        DeviceType GetDeviceTypeFromClass(vr::ETrackedDeviceClass device_class);
    };
} 