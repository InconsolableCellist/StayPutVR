#include "VRDriver.hpp"
#include "../UI/UIManager.hpp"
#include "../Logger.hpp"

// Define the global variable
std::atomic<bool> StayPutVR::g_running(true);

StayPutVR::VRDriver::VRDriver() {
    Logger::Info("VRDriver: Constructor called");
    // Initialize any driver-specific variables here
}

StayPutVR::VRDriver::~VRDriver() {
    Logger::Info("VRDriver: Destructor called");
    // Make sure UI thread exits properly
    g_running = false;
    
    // Wait for the UI thread to terminate if it's joinable
    if (ui_thread_.joinable()) {
        Logger::Info("VRDriver: Waiting for UI thread to exit");
        ui_thread_.join();
        Logger::Info("VRDriver: UI thread joined");
    }
}

void StayPutVR::VRDriver::StartUIThread() {
    Logger::Info("VRDriver: Starting UI thread");
    // Start UI in separate thread
    ui_thread_ = std::thread([this]() {
        try {
            // Log that we're starting the UI
            this->Log("Starting StayPutVR UI thread...");
            Logger::Info("UI Thread: Starting");
            
            StayPutVR::UIManager ui_manager;
            
            // Initialize the UI
            Logger::Info("UI Thread: Initializing UI manager");
            if (!ui_manager.Initialize()) {
                // Handle initialization error
                this->Log("Failed to initialize StayPutVR UI!");
                Logger::Error("UI Thread: Failed to initialize UI manager");
                g_running = false;
                return;
            }
            
            this->Log("StayPutVR UI initialized successfully");
            Logger::Info("UI Thread: UI manager initialized successfully");
            
            // Main UI loop
            Logger::Info("UI Thread: Entering main loop");
            while (g_running) {
                try {
                    ui_manager.Update();
                    
                    // Get device positions from all tracked devices in the system
                    auto device_infos = this->GetAllTrackedDeviceInfo();
                    
                    // Convert TrackedDeviceInfo to IVRDevice for UI
                    std::vector<std::shared_ptr<IVRDevice>> ui_devices;
                    for (const auto& info : device_infos) {
                        // Create a simple wrapper device
                        class TrackedDeviceWrapper : public IVRDevice {
                        public:
                            TrackedDeviceWrapper(const TrackedDeviceInfo& info) 
                                : info_(info) {}
                            
                            virtual std::string GetSerial() override { return info_.serial; }
                            virtual void Update() override {}
                            virtual vr::TrackedDeviceIndex_t GetDeviceIndex() override { return info_.device_index; }
                            virtual DeviceType GetDeviceType() override { return info_.type; }
                            
                            // ITrackedDeviceServerDriver interface stubs (not used by UI)
                            virtual vr::EVRInitError Activate(uint32_t unObjectId) override { return vr::VRInitError_None; }
                            virtual void Deactivate() override {}
                            virtual void EnterStandby() override {}
                            virtual void* GetComponent(const char* pchComponentNameAndVersion) override { return nullptr; }
                            virtual void DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize) override {}
                            virtual vr::DriverPose_t GetPose() override { return info_.pose; }
                        
                        private:
                            TrackedDeviceInfo info_;
                        };
                        
                        ui_devices.push_back(std::make_shared<TrackedDeviceWrapper>(info));
                    }
                    
                    ui_manager.UpdateDevicePositions(ui_devices);
                    
                    // Render the UI
                    ui_manager.Render();
                    
                    // Small sleep to prevent high CPU usage
                    std::this_thread::sleep_for(std::chrono::milliseconds(10)); // ~100fps
                }
                catch (const std::exception& e) {
                    Logger::Error("UI Thread: Exception in UI loop: " + std::string(e.what()));
                }
                catch (...) {
                    Logger::Error("UI Thread: Unknown exception in UI loop");
                }
            }
            
            // Cleanup UI
            Logger::Info("UI Thread: Shutting down UI manager");
            ui_manager.Shutdown();
            this->Log("StayPutVR UI thread shut down");
            Logger::Info("UI Thread: UI thread exiting normally");
        }
        catch (const std::exception& e) {
            Logger::Error("UI Thread: Critical exception: " + std::string(e.what()));
            g_running = false;
        }
        catch (...) {
            Logger::Error("UI Thread: Unknown critical exception");
            g_running = false;
        }
    });
}

DeviceType StayPutVR::VRDriver::GetDeviceTypeFromClass(vr::ETrackedDeviceClass device_class) {
    switch (device_class) {
        case vr::TrackedDeviceClass_HMD:
            return DeviceType::HMD;
        case vr::TrackedDeviceClass_Controller:
            return DeviceType::CONTROLLER;
        case vr::TrackedDeviceClass_GenericTracker:
            return DeviceType::TRACKER;
        case vr::TrackedDeviceClass_TrackingReference:
            return DeviceType::TRACKING_REFERENCE;
        default:
            return DeviceType::CONTROLLER; // Default
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
                info.type = GetDeviceTypeFromClass(deviceClass);
                
                // Get device serial from property
                char serialBuffer[256];
                vr::VRProperties()->GetStringProperty(props, vr::Prop_SerialNumber_String, 
                    serialBuffer, sizeof(serialBuffer), &error);
                    
                if (error == vr::TrackedProp_Success) {
                    info.serial = serialBuffer;
                    
                    // Get the latest pose from the tracking system
                    info.pose = IVRDevice::MakeDefaultPose();
                    
                    // Copy position and rotation from the tracked device pose
                    if (trackedDevicePoses[idx].bPoseIsValid) {
                        const auto& mat = trackedDevicePoses[idx].mDeviceToAbsoluteTracking;
                        
                        // Extract position from the transformation matrix
                        info.pose.vecPosition[0] = mat.m[0][3];
                        info.pose.vecPosition[1] = mat.m[1][3];
                        info.pose.vecPosition[2] = mat.m[2][3];
                        
                        // Convert rotation matrix to quaternion
                        // This is a simplified conversion that works for most cases
                        float w = sqrt(1.0f + mat.m[0][0] + mat.m[1][1] + mat.m[2][2]) / 2.0f;
                        float w4 = 4.0f * w;
                        info.pose.qRotation.x = (mat.m[2][1] - mat.m[1][2]) / w4;
                        info.pose.qRotation.y = (mat.m[0][2] - mat.m[2][0]) / w4;
                        info.pose.qRotation.z = (mat.m[1][0] - mat.m[0][1]) / w4;
                        info.pose.qRotation.w = w;
                        
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
        
        // Start the UI thread
        Logger::Info("VRDriver: Starting UI thread");
        StartUIThread();
        
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
    // Signal the UI thread to exit
    g_running = false;
    
    // Wait for the UI thread to terminate if it's joinable
    if (ui_thread_.joinable()) {
        ui_thread_.join();
    }
    
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