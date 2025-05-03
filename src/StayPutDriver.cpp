#include "StayPutDriver.h"
#include "ui/StayPutUI.h"
#include <iostream>
#include <chrono>
#include <cmath>

StayPutDriver::StayPutDriver() : isTracking(false), shouldExit(false), sessionStartTime(0) {
}

StayPutDriver::~StayPutDriver() {
    StopTracking();
    Cleanup();
}

vr::EVRInitError StayPutDriver::Init(vr::IVRDriverContext* pDriverContext) {
    vr::EVRInitError initError = vr::VRInitError_None;
    
    // Initialize the driver context
    driverContext = pDriverContext;
    vr::VR_INIT_SERVER_DRIVER_CONTEXT(driverContext);
    
    // Get the server driver host interface
    vrServer = vr::VRServerDriverHost();
    if (!vrServer) {
        return vr::VRInitError_Init_InterfaceNotFound;
    }
    
    // Initialize UI
    ui = std::make_shared<StayPutUI>(this);
    ui->Start();
    
    return initError;
}

void StayPutDriver::Cleanup() {
    if (ui) {
        ui->Stop();
        ui.reset();
    }
    
    vr::VR_CLEANUP_SERVER_DRIVER_CONTEXT();
}

void StayPutDriver::RunFrame() {
    if (isTracking) {
        CaptureDeviceData();
    }
}

void StayPutDriver::CaptureDeviceData() {
    std::lock_guard<std::mutex> lock(devicesMutex);
    
    // Update device poses
    for (auto& pair : trackedDevices) {
        if (pair.second.freezeReference.isFrozen) {
            // If device is frozen, maintain its position
            vr::DriverPose_t pose = {};
            pose.poseIsValid = true;
            pose.result = vr::TrackingResult_Running_OK;
            pose.deviceIsConnected = true;
            pose.qWorldFromDriverRotation = {1, 0, 0, 0};
            pose.vecPosition[0] = pair.second.freezeReference.position.v[0];
            pose.vecPosition[1] = pair.second.freezeReference.position.v[1];
            pose.vecPosition[2] = pair.second.freezeReference.position.v[2];
            
            // Convert string device ID to uint32_t
            uint32_t deviceId = static_cast<uint32_t>(std::stoul(pair.first));
            vrServer->TrackedDevicePoseUpdated(deviceId, pose, sizeof(vr::DriverPose_t));
        }
    }
}

void StayPutDriver::StartTracking() {
    if (!isTracking) {
        isTracking = true;
        sessionStartTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        
        trackingThread = std::thread(&StayPutDriver::CaptureDataLoop, this);
    }
}

void StayPutDriver::StopTracking() {
    if (isTracking) {
        isTracking = false;
        shouldExit = true;
        if (trackingThread.joinable()) {
            trackingThread.join();
        }
    }
}

void StayPutDriver::CaptureDataLoop() {
    while (isTracking && !shouldExit) {
        CaptureDeviceData();
        std::this_thread::sleep_for(std::chrono::milliseconds(125)); // 8Hz
    }
}

void StayPutDriver::FreezeTrackers(const std::vector<std::string>& trackerSerials) {
    std::lock_guard<std::mutex> lock(devicesMutex);
    
    if (trackerSerials.empty()) {
        // Freeze all trackers
        for (auto& pair : trackedDevices) {
            if (!pair.second.data.empty()) {
                const auto& latestPose = pair.second.data.back().pose;
                
                pair.second.freezeReference.isFrozen = true;
                pair.second.freezeReference.position.v[0] = static_cast<float>(latestPose.vecPosition[0]);
                pair.second.freezeReference.position.v[1] = static_cast<float>(latestPose.vecPosition[1]);
                pair.second.freezeReference.position.v[2] = static_cast<float>(latestPose.vecPosition[2]);
            }
        }
    } else {
        // Freeze specific trackers
        for (const auto& serial : trackerSerials) {
            auto it = trackedDevices.find(serial);
            if (it != trackedDevices.end() && !it->second.data.empty()) {
                const auto& latestPose = it->second.data.back().pose;
                
                it->second.freezeReference.isFrozen = true;
                it->second.freezeReference.position.v[0] = static_cast<float>(latestPose.vecPosition[0]);
                it->second.freezeReference.position.v[1] = static_cast<float>(latestPose.vecPosition[1]);
                it->second.freezeReference.position.v[2] = static_cast<float>(latestPose.vecPosition[2]);
            }
        }
    }
}

float StayPutDriver::CalculateDistanceFromFrozen(const std::string& trackerSerial) {
    std::lock_guard<std::mutex> lock(devicesMutex);
    
    auto it = trackedDevices.find(trackerSerial);
    if (it == trackedDevices.end() || !it->second.freezeReference.isFrozen || it->second.data.empty()) {
        return -1.0f; // Invalid tracker or not frozen
    }
    
    const auto& latestPose = it->second.data.back().pose;
    const auto& reference = it->second.freezeReference.position;
    
    float dx = static_cast<float>(latestPose.vecPosition[0] - reference.v[0]);
    float dy = static_cast<float>(latestPose.vecPosition[1] - reference.v[1]);
    float dz = static_cast<float>(latestPose.vecPosition[2] - reference.v[2]);
    
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}
