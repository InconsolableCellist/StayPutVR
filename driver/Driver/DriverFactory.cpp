#include "DriverFactory.hpp"
#include <thread>
#include <filesystem>
#include "../Driver/VRDriver.hpp"
#include <Windows.h>
#include <sstream>
#include "../../common/Logger.hpp"
#include "../../common/PathUtils.hpp"

static std::shared_ptr<StayPutVR::VRDriver> driver;

// This is the main entry point for the driver
void* HmdDriverFactory(const char* interface_name, int* return_code) {
    try {
        if (std::strcmp(interface_name, vr::IServerTrackedDeviceProvider_Version) == 0) {
            if (!driver) {
                // Initialize logger first
                try {
                    // Use AppData path for driver logs
                    std::string appDataPath = StayPutVR::GetAppDataPath();
                    std::string logPath = appDataPath + "\\logs";
                    
                    // Create logs directory if it doesn't exist
                    if (!std::filesystem::exists(logPath)) {
                        std::filesystem::create_directories(logPath);
                    }
                    
                    // Initialize with DRIVER log type
                    StayPutVR::Logger::Init(logPath, StayPutVR::Logger::LogType::DRIVER);
                    StayPutVR::Logger::Info("StayPutVR driver starting up");
                    StayPutVR::Logger::Info("Log path: " + logPath);
                }
                catch (const std::exception& e) {
                    // Can't use logger yet, so just output to stderr
                    std::cerr << "Failed to initialize logger: " << e.what() << std::endl;
                }
                
                // Instantiate concrete implementation
                try {
                    driver = std::make_shared<StayPutVR::VRDriver>();
                    StayPutVR::Logger::Info("Driver instance created successfully");
                }
                catch (const std::exception& e) {
                    if (StayPutVR::Logger::IsInitialized()) {
                        StayPutVR::Logger::Critical("Exception creating driver instance: " + std::string(e.what()));
                    }
                    std::cerr << "Exception creating driver instance: " << e.what() << std::endl;
                    
                    if (return_code) {
                        *return_code = vr::VRInitError_Driver_Failed;
                    }
                    return nullptr;
                }
            }
            // We always have at least 1 reference to the shared ptr in "driver" so passing out raw pointer is ok
            return driver.get();
        }

        if (return_code)
            *return_code = vr::VRInitError_Init_InterfaceNotFound;
    }
    catch (const std::exception& e) {
        if (StayPutVR::Logger::IsInitialized()) {
            StayPutVR::Logger::Critical("Exception in HmdDriverFactory: " + std::string(e.what()));
        }
        std::cerr << "Exception in HmdDriverFactory: " << e.what() << std::endl;
        
        if (return_code) {
            *return_code = vr::VRInitError_Driver_Failed;
        }
    }
    catch (...) {
        if (StayPutVR::Logger::IsInitialized()) {
            StayPutVR::Logger::Critical("Unknown exception in HmdDriverFactory");
        }
        std::cerr << "Unknown exception in HmdDriverFactory" << std::endl;
        
        if (return_code) {
            *return_code = vr::VRInitError_Driver_Failed;
        }
    }

    return nullptr;
}

std::shared_ptr<StayPutVR::IVRDriver> StayPutVR::GetDriver() {
    return driver;
} 