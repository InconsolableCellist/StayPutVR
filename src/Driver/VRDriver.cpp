#include "VRDriver.hpp"

vr::EVRInitError StayPutVR::VRDriver::Init(vr::IVRDriverContext* pDriverContext)
{
    // Perform driver context initialisation
    if (vr::EVRInitError init_error = vr::InitServerDriverContext(pDriverContext); init_error != vr::EVRInitError::VRInitError_None) {
        return init_error;
    }

    Log("Activating StayPutVR driver...");

    // We don't need to add any devices here since we're just tracking existing ones
    // In a full driver you might add virtual devices here
    
    Log("StayPutVR driver loaded successfully");

    return vr::VRInitError_None;
}

void StayPutVR::VRDriver::Cleanup()
{
    // Clean up any resources you've created
    Log("StayPutVR driver shutting down");
}

void StayPutVR::VRDriver::RunFrame()
{
    // Collect events
    vr::VREvent_t event;
    std::vector<vr::VREvent_t> events;
    while (vr::VRServerDriverHost()->PollNextEvent(&event, sizeof(event)))
    {
        events.push_back(event);
    }
    this->openvr_events_ = events;

    // Update frame timing
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    this->frame_timing_ = std::chrono::duration_cast<std::chrono::milliseconds>(now - this->last_frame_time_);
    this->last_frame_time_ = now;

    // Update devices
    for (auto& device : this->devices_)
        device->Update();
}

bool StayPutVR::VRDriver::ShouldBlockStandbyMode()
{
    return false;
}

void StayPutVR::VRDriver::EnterStandby()
{
}

void StayPutVR::VRDriver::LeaveStandby()
{
}

std::vector<std::shared_ptr<StayPutVR::IVRDevice>> StayPutVR::VRDriver::GetDevices()
{
    return this->devices_;
}

std::vector<vr::VREvent_t> StayPutVR::VRDriver::GetOpenVREvents()
{
    return this->openvr_events_;
}

std::chrono::milliseconds StayPutVR::VRDriver::GetLastFrameTime()
{
    return this->frame_timing_;
}

bool StayPutVR::VRDriver::AddDevice(std::shared_ptr<IVRDevice> device)
{
    vr::ETrackedDeviceClass openvr_device_class;
    
    // Match device type to OpenVR device class
    switch (device->GetDeviceType()) {
        case DeviceType::CONTROLLER:
            openvr_device_class = vr::ETrackedDeviceClass::TrackedDeviceClass_Controller;
            break;
        case DeviceType::HMD:
            openvr_device_class = vr::ETrackedDeviceClass::TrackedDeviceClass_HMD;
            break;
        case DeviceType::TRACKER:
            openvr_device_class = vr::ETrackedDeviceClass::TrackedDeviceClass_GenericTracker;
            break;
        case DeviceType::TRACKING_REFERENCE:
            openvr_device_class = vr::ETrackedDeviceClass::TrackedDeviceClass_TrackingReference;
            break;
        default:
            return false;
    }
    
    bool result = vr::VRServerDriverHost()->TrackedDeviceAdded(device->GetSerial().c_str(), openvr_device_class, device.get());
    if(result)
        this->devices_.push_back(device);
    return result;
}

StayPutVR::SettingsValue StayPutVR::VRDriver::GetSettingsValue(std::string key)
{
    vr::EVRSettingsError err = vr::EVRSettingsError::VRSettingsError_None;
    
    // Try to get different types of settings
    int int_value = vr::VRSettings()->GetInt32(settings_key_.c_str(), key.c_str(), &err);
    if (err == vr::EVRSettingsError::VRSettingsError_None) {
        return int_value;
    }
    
    err = vr::EVRSettingsError::VRSettingsError_None;
    float float_value = vr::VRSettings()->GetFloat(settings_key_.c_str(), key.c_str(), &err);
    if (err == vr::EVRSettingsError::VRSettingsError_None) {
        return float_value;
    }
    
    err = vr::EVRSettingsError::VRSettingsError_None;
    bool bool_value = vr::VRSettings()->GetBool(settings_key_.c_str(), key.c_str(), &err);
    if (err == vr::EVRSettingsError::VRSettingsError_None) {
        return bool_value;
    }
    
    std::string str_value;
    str_value.reserve(1024);
    vr::VRSettings()->GetString(settings_key_.c_str(), key.c_str(), str_value.data(), 1024, &err);
    if (err == vr::EVRSettingsError::VRSettingsError_None) {
        return str_value;
    }
    
    return SettingsValue();
}

void StayPutVR::VRDriver::Log(std::string message)
{
    std::string message_endl = message + "\n";
    vr::VRDriverLog()->Log(message_endl.c_str());
}

vr::IVRDriverInput* StayPutVR::VRDriver::GetInput()
{
    return vr::VRDriverInput();
}

vr::CVRPropertyHelpers* StayPutVR::VRDriver::GetProperties()
{
    return vr::VRProperties();
}

vr::IVRServerDriverHost* StayPutVR::VRDriver::GetDriverHost()
{
    return vr::VRServerDriverHost();
} 