/**
 * StayPutVR - A SteamVR driver for position enforcement
 * 
 * main.cpp - Entry point for the driver DLL
 */

#include <openvr_driver.h>
#include <memory>
#include <iostream>
#include <thread>
#include "StayPutDriver.h"

#if defined(_WIN32)
#define EXPORT_API __declspec(dllexport)
#else
#define EXPORT_API
#endif

// Global driver instance that will be accessed by SteamVR
std::unique_ptr<StayPutDriver> g_driverInstance;
std::thread g_driverThread;
bool g_isInitialized = false;

/**
 * Initializes the driver if it hasn't been already.
 * This function starts the driver in a separate thread to avoid blocking
 * SteamVR's driver loading process.
 */
void EnsureDriverInitialized() {
    if (!g_isInitialized) {
        g_driverInstance = std::make_unique<StayPutDriver>();
        
        // Start the driver in a separate thread
        g_driverThread = std::thread([]() {
            if (g_driverInstance->Initialize()) {
                std::cout << "StayPutVR driver initialized successfully!" << std::endl;
            } else {
                std::cerr << "Failed to initialize StayPutVR driver!" << std::endl;
            }
        });
        
        // Detach the thread so it can run independently
        g_driverThread.detach();
        g_isInitialized = true;
    }
}

/**
 * Implements the VR::IServerTrackedDeviceProvider interface, which SteamVR
 * uses to discover and load the driver.
 */
class ServerTrackedDeviceProvider : public vr::IServerTrackedDeviceProvider {
public:
    /**
     * Initialize the driver provider
     */
    vr::EVRInitError Init(vr::IVRDriverContext* pDriverContext) override {
        // Initialize the OpenVR API
        vr::EVRInitError eError = vr::InitServerDriverContext(pDriverContext);
        if (eError != vr::VRInitError_None) {
            return eError;
        }
        
        // Initialize our main driver
        EnsureDriverInitialized();
        
        return vr::VRInitError_None;
    }
    
    /**
     * Cleanup before shutting down
     */
    void Cleanup() override {
        if (g_driverInstance) {
            g_driverInstance->Shutdown();
            g_driverInstance.reset();
        }
        
        g_isInitialized = false;
        vr::CleanupDriverContext();
    }
    
    /**
     * Returns the OpenVR API version this driver was built with
     */
    const char* const* GetInterfaceVersions() override {
        return vr::k_InterfaceVersions;
    }
    
    /**
     * SteamVR will call this method periodically to give the driver
     * a chance to do background work
     */
    void RunFrame() override {
        // Our driver does its work in its own thread, so nothing to do here
    }
    
    /**
     * Returns whether this driver handles discovery and connection events
     */
    bool ShouldBlockStandbyMode() override {
        return false;
    }
    
    /**
     * Called when the system is entering standby mode
     */
    void EnterStandby() override {
        // No special handling needed
    }
    
    /**
     * Called when the system is leaving standby mode
     */
    void LeaveStandby() override {
        // No special handling needed
    }
};

// Static instance of our tracked device provider
static ServerTrackedDeviceProvider g_serverTrackedDeviceProvider;

extern "C" EXPORT_API void* HmdDriverFactory(const char* pInterfaceName, int* pReturnCode) {
    if (std::string(pInterfaceName) == vr::IServerTrackedDeviceProvider_Version) {
        auto driver = new StayPutDriver();
        return driver;
    }
    
    if (pReturnCode) {
        *pReturnCode = vr::VRInitError_Init_InterfaceNotFound;
    }
    
    return nullptr;
}

/**
 * Optional: DLL entry point for Windows
 * This can be used for additional initialization if needed
 */
#ifdef _WIN32
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            // DLL is being loaded
            break;
        case DLL_PROCESS_DETACH:
            // DLL is being unloaded
            break;
    }
    return TRUE;
}
#endif