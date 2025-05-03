#pragma once
#include <string>
#include <vector>
#include <openvr_driver.h>

// Structure to store single snapshot of tracker data
struct InstantaneousTrackedData {
    vr::DriverPose_t pose;
    long long timestamp;
};

// Structure to store tracker reference data for freeze functionality
struct FreezeReference {
    bool isFrozen;
    vr::HmdVector3_t position;
    vr::HmdQuaternion_t rotation;
};

// Structure to store data for a single tracked device
struct TrackedDeviceData {
    std::string serialNumber;
    std::string deviceClass;
    vr::TrackedDeviceIndex_t deviceId;
    FreezeReference freezeReference;
    std::vector<InstantaneousTrackedData> data;
};