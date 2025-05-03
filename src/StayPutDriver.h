#pragma once

// Include only the driver header
#include <openvr_driver.h>

#include <unordered_map>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <chrono>
#include "TrackedDeviceData.h"

class StayPutUI;

class StayPutDriver : public vr::IServerTrackedDeviceProvider {
public:
    StayPutDriver();
    ~StayPutDriver();
    
    // Core functionality
    vr::EVRInitError Init(vr::IVRDriverContext* pDriverContext) override;
    void Cleanup() override;
    void RunFrame() override;
    
    // Tracking control
    void StartTracking();
    void StopTracking();
    void SaveData(const std::string& filename = "");
    
    // Freeze functionality
    void FreezeTrackers(const std::vector<std::string>& trackerSerials = {});
    void UnfreezeTrackers(const std::vector<std::string>& trackerSerials = {});
    
    // Status info
    std::unordered_map<std::string, TrackedDeviceData> GetTrackedDevices();
    std::string GetStatus();
    bool IsTracking() const { return isTracking; }
    
private:
    // Internal methods
    void CaptureDataLoop();
    void CaptureDeviceData();
    std::string GetTrackedDeviceSerial(vr::TrackedDeviceIndex_t deviceIndex);
    std::string GetTrackedDeviceClass(vr::TrackedDeviceIndex_t deviceIndex);
    void UpdateDeviceList();
    float CalculateDistanceFromFrozen(const std::string& trackerSerial);
    
    // Data members
    std::unordered_map<std::string, TrackedDeviceData> trackedDevices;
    vr::IVRServerDriverHost* vrServer{nullptr};
    vr::IVRDriverContext* driverContext{nullptr};
    
    std::atomic<bool> isTracking;
    std::atomic<bool> shouldExit;
    
    std::thread trackingThread;
    std::mutex devicesMutex;
    
    long long sessionStartTime;
    std::shared_ptr<StayPutUI> ui;
};
