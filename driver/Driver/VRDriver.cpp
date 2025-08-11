#include "VRDriver.hpp"
#include "../../../common/Logger.hpp"
#include "../IPC/IPCServer.hpp"

// Define the global variable
std::atomic<bool> StayPutVR::g_running(true);

StayPutVR::VRDriver::VRDriver() {
    Logger::Info("VRDriver: Constructor called");
    // Initialize any driver-specific variables here
}

StayPutVR::VRDriver::~VRDriver() {
    Logger::Info("VRDriver: Destructor called");
    // Make sure IPC server is shut down
    ipc_server_.Shutdown();
}

StayPutVR::DeviceType StayPutVR::VRDriver::GetDeviceTypeFromClass(vr::ETrackedDeviceClass device_class) {
    switch (device_class) {
        case vr::TrackedDeviceClass_HMD:
            return StayPutVR::DeviceType::HMD;
        case vr::TrackedDeviceClass_Controller:
            return StayPutVR::DeviceType::CONTROLLER;
        case vr::TrackedDeviceClass_GenericTracker:
            return StayPutVR::DeviceType::TRACKER;
        case vr::TrackedDeviceClass_TrackingReference:
            return StayPutVR::DeviceType::TRACKING_REFERENCE;
        default:
            return StayPutVR::DeviceType::CONTROLLER; // Default
    }
}

std::vector<StayPutVR::TrackedDeviceInfo> StayPutVR::VRDriver::GetAllTrackedDeviceInfo() {
    std::vector<TrackedDeviceInfo> result;
    
    try {
        // Get the list of tracked devices in the system
        // Note: We use the public SteamVR API here, not the internal server API
        // because the driver host interface doesn't provide these functions
        
        // Get the current tracking state for all devices
        vr::TrackedDevicePose_t trackedDevicePoses[vr::k_unMaxTrackedDeviceCount];
        vr::VRServerDriverHost()->GetRawTrackedDevicePoses(0, trackedDevicePoses, vr::k_unMaxTrackedDeviceCount);
        
        Logger::Debug("VRDriver: Getting all tracked device info");
        
        // Since we can't directly access the device class, we'll iterate through all possible devices
        // and check if they're valid by seeing if they have valid poses in the system
        for (vr::TrackedDeviceIndex_t idx = 0; idx < vr::k_unMaxTrackedDeviceCount; idx++) {
            // Check if the device index is valid by accessing its properties
            auto props = vr::VRProperties()->TrackedDeviceToPropertyContainer(idx);
            
            // Try to get device class property
            vr::ETrackedPropertyError error;
            vr::ETrackedDeviceClass deviceClass = (vr::ETrackedDeviceClass)vr::VRProperties()->GetInt32Property(
                props, vr::Prop_DeviceClass_Int32, &error);
                
            // Only process valid devices (not invalid class)
            if (error == vr::TrackedProp_Success && deviceClass != vr::TrackedDeviceClass_Invalid) {
                TrackedDeviceInfo info;
                info.device_index = idx;
                info.type = this->GetDeviceTypeFromClass(deviceClass);
                
                // Get device serial from property
                char serialBuffer[256] = {};  // Initialize to zeros
                vr::VRProperties()->GetStringProperty(props, vr::Prop_SerialNumber_String, 
                    serialBuffer, sizeof(serialBuffer), &error);
                    
                if (error == vr::TrackedProp_Success) {
                    // Validate the serial number
                    if (serialBuffer[0] == '\0') {
                        Logger::Warning("VRDriver: Device has empty serial number, skipping");
                        continue;
                    }
                    
                    info.serial = serialBuffer;
                    Logger::Debug("VRDriver: Found device with serial: " + info.serial);
                    
                    // Get the latest pose from the tracking system
                    info.pose = IVRDevice::MakeDefaultPose();
                    
                    // Copy position and rotation from the tracked device pose
                    if (trackedDevicePoses[idx].bPoseIsValid) {
                        const auto& mat = trackedDevicePoses[idx].mDeviceToAbsoluteTracking;
                        
                        // Extract position from the transformation matrix
                        info.pose.vecPosition[0] = mat.m[0][3];
                        info.pose.vecPosition[1] = mat.m[1][3];
                        info.pose.vecPosition[2] = mat.m[2][3];
                        
                        try {
                            // Convert rotation matrix to quaternion
                            // This is a simplified conversion that works for most cases
                            float trace = mat.m[0][0] + mat.m[1][1] + mat.m[2][2];
                            
                            if (trace > 0) {
                                float w = sqrt(1.0f + trace) / 2.0f;
                                float w4 = 4.0f * w;
                                info.pose.qRotation.x = (mat.m[2][1] - mat.m[1][2]) / w4;
                                info.pose.qRotation.y = (mat.m[0][2] - mat.m[2][0]) / w4;
                                info.pose.qRotation.z = (mat.m[1][0] - mat.m[0][1]) / w4;
                                info.pose.qRotation.w = w;
                            } else if (mat.m[0][0] > mat.m[1][1] && mat.m[0][0] > mat.m[2][2]) {
                                // Column 0 has largest diagonal value
                                float s = sqrt(1.0f + mat.m[0][0] - mat.m[1][1] - mat.m[2][2]) * 2.0f;
                                info.pose.qRotation.x = 0.25f * s;
                                info.pose.qRotation.y = (mat.m[0][1] + mat.m[1][0]) / s;
                                info.pose.qRotation.z = (mat.m[0][2] + mat.m[2][0]) / s;
                                info.pose.qRotation.w = (mat.m[2][1] - mat.m[1][2]) / s;
                            } else if (mat.m[1][1] > mat.m[2][2]) {
                                // Column 1 has largest diagonal value
                                float s = sqrt(1.0f + mat.m[1][1] - mat.m[0][0] - mat.m[2][2]) * 2.0f;
                                info.pose.qRotation.x = (mat.m[0][1] + mat.m[1][0]) / s;
                                info.pose.qRotation.y = 0.25f * s;
                                info.pose.qRotation.z = (mat.m[1][2] + mat.m[2][1]) / s;
                                info.pose.qRotation.w = (mat.m[0][2] - mat.m[2][0]) / s;
                            } else {
                                // Column 2 has largest diagonal value
                                float s = sqrt(1.0f + mat.m[2][2] - mat.m[0][0] - mat.m[1][1]) * 2.0f;
                                info.pose.qRotation.x = (mat.m[0][2] + mat.m[2][0]) / s;
                                info.pose.qRotation.y = (mat.m[1][2] + mat.m[2][1]) / s;
                                info.pose.qRotation.z = 0.25f * s;
                                info.pose.qRotation.w = (mat.m[1][0] - mat.m[0][1]) / s;
                            }
                            
                            // Normalize quaternion to ensure it's valid
                            float length = sqrt(
                                info.pose.qRotation.x * info.pose.qRotation.x +
                                info.pose.qRotation.y * info.pose.qRotation.y +
                                info.pose.qRotation.z * info.pose.qRotation.z +
                                info.pose.qRotation.w * info.pose.qRotation.w
                            );
                            
                            if (length > 0.0001f) {
                                info.pose.qRotation.x /= length;
                                info.pose.qRotation.y /= length;
                                info.pose.qRotation.z /= length;
                                info.pose.qRotation.w /= length;
                            } else {
                                // Default to identity quaternion if conversion failed
                                Logger::Warning("VRDriver: Quaternion conversion failed, using identity");
                                info.pose.qRotation.x = 0.0f;
                                info.pose.qRotation.y = 0.0f;
                                info.pose.qRotation.z = 0.0f;
                                info.pose.qRotation.w = 1.0f;
                            }
                        }
                        catch (const std::exception& e) {
                            Logger::Error("VRDriver: Exception in quaternion conversion: " + std::string(e.what()));
                            // Default to identity quaternion
                            info.pose.qRotation.x = 0.0f;
                            info.pose.qRotation.y = 0.0f;
                            info.pose.qRotation.z = 0.0f;
                            info.pose.qRotation.w = 1.0f;
                        }
                        
                        // Set tracking result based on the pose validity
                        info.pose.poseIsValid = true;
                        info.pose.result = vr::TrackingResult_Running_OK;
                    } else {
                        // If pose is not valid, mark it as such
                        info.pose.poseIsValid = false;
                        info.pose.result = vr::TrackingResult_Running_OutOfRange;
                    }
                    
                    result.push_back(info);
                }
            }
        }
    }
    catch (const std::exception& e) {
        Logger::Error("VRDriver: Exception in GetAllTrackedDeviceInfo: " + std::string(e.what()));
    }
    catch (...) {
        Logger::Error("VRDriver: Unknown exception in GetAllTrackedDeviceInfo");
    }
    
    return result;
}

vr::EVRInitError StayPutVR::VRDriver::Init(vr::IVRDriverContext* pDriverContext)
{
    Logger::Info("VRDriver: Init called");
    try {
        // Perform driver context initialisation
        if (vr::EVRInitError init_error = vr::InitServerDriverContext(pDriverContext); init_error != vr::EVRInitError::VRInitError_None) {
            Logger::Error("VRDriver: Failed to initialize server driver context: " + std::to_string(init_error));
            return init_error;
        }

        Log("Activating StayPutVR driver...");
        Logger::Info("VRDriver: Driver context initialized successfully");

        // We don't need to add any devices here since we're just tracking existing ones
        // In a full driver you might add virtual devices here
        
        // IPC is always enabled - it's the core purpose of this driver
        // Initialize IPC immediately to prevent blocking in RunFrame
        Logger::Info("VRDriver: Initializing IPC server during driver init");
        if (!ipc_server_.Initialize()) {
            Logger::Warning("VRDriver: Failed to initialize IPC server, but continuing (non-critical)");
            // Don't fail driver init - VR should work without companion app
        } else {
            Logger::Info("VRDriver: IPC server initialized successfully");
        }
        
        Log("StayPutVR driver loaded successfully");
        Logger::Info("VRDriver: Driver loaded successfully");
        return vr::VRInitError_None;
    }
    catch (const std::exception& e) {
        Logger::Error("VRDriver: Exception in Init: " + std::string(e.what()));
        return vr::VRInitError_Driver_Failed;
    }
    catch (...) {
        Logger::Error("VRDriver: Unknown exception in Init");
        return vr::VRInitError_Driver_Failed;
    }
}

void StayPutVR::VRDriver::Cleanup()
{
    // Shut down IPC server
    ipc_server_.Shutdown();
    
    // Clean up any resources you've created
    Log("StayPutVR driver shutting down");
}

void StayPutVR::VRDriver::RunFrame()
{
    try {
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

        // IPC is always enabled for this driver - proceed with lazy initialization

        // Lazy initialization: Only initialize IPC when we have data to send
        // This prevents aggressive connection attempts when no companion app is available
        
        // Collect device positions from all tracked devices
        auto tracked_devices = GetAllTrackedDeviceInfo();
        
        // Only attempt IPC operations if we have devices to send
        if (!tracked_devices.empty()) {
            // Convert to DevicePositionData for IPC
            std::vector<DevicePositionData> device_positions;
            for (const auto& device : tracked_devices) {
                try {
                    DevicePositionData pos_data;
                    pos_data.serial = device.serial;
                    pos_data.type = device.type;
                    
                    // Get position and rotation from device pose
                    pos_data.position[0] = device.pose.vecPosition[0];
                    pos_data.position[1] = device.pose.vecPosition[1];
                    pos_data.position[2] = device.pose.vecPosition[2];
                    
                    pos_data.rotation[0] = device.pose.qRotation.x;
                    pos_data.rotation[1] = device.pose.qRotation.y;
                    pos_data.rotation[2] = device.pose.qRotation.z;
                    pos_data.rotation[3] = device.pose.qRotation.w;
                    
                    pos_data.connected = device.pose.deviceIsConnected;
                    
                    device_positions.push_back(pos_data);
                }
                catch (const std::exception& e) {
                    // Silently continue - don't spam logs in VR frame loop
                    continue;
                }
            }
            
            // Send device positions via IPC - completely non-blocking
            try {
                ipc_server_.SendDeviceUpdates(device_positions);
            }
            catch (...) {
                // Silently handle IPC errors - don't let them affect VR performance
                // IPC is non-critical for VR operation
            }
        }
        
        // Process incoming messages only if IPC is connected - non-blocking
        if (ipc_server_.IsConnected()) {
            try {
                ipc_server_.ProcessIncomingMessages();
            }
            catch (...) {
                // Silently handle IPC errors - don't let them affect VR performance
                // IPC is non-critical for VR operation
            }
        }
    }
    catch (const std::exception& e) {
        Logger::Error("VRDriver: Exception in RunFrame: " + std::string(e.what()));
    }
    catch (...) {
        Logger::Error("VRDriver: Unknown exception in RunFrame");
    }
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
        case StayPutVR::DeviceType::CONTROLLER:
            openvr_device_class = vr::ETrackedDeviceClass::TrackedDeviceClass_Controller;
            break;
        case StayPutVR::DeviceType::HMD:
            openvr_device_class = vr::ETrackedDeviceClass::TrackedDeviceClass_HMD;
            break;
        case StayPutVR::DeviceType::TRACKER:
            openvr_device_class = vr::ETrackedDeviceClass::TrackedDeviceClass_GenericTracker;
            break;
        case StayPutVR::DeviceType::TRACKING_REFERENCE:
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