#include <thread>
#include <atomic>
#include <memory>
#include "Driver/VRDriver.hpp"
#include "UI/UIManager.hpp"

// Global variables for communication between driver and UI
std::atomic<bool> g_running = true;
std::shared_ptr<StayPutVR::VRDriver> g_driver;

int main(int argc, char* argv[]) {
    // Initialize the driver
    g_driver = std::make_shared<StayPutVR::VRDriver>();
    
    // Start the UI in a separate thread
    std::thread ui_thread([]() {
        StayPutVR::UIManager ui_manager;
        
        // Initialize the UI
        if (!ui_manager.Initialize()) {
            // Handle initialization error
            g_running = false;
            return;
        }
        
        // Main UI loop
        while (g_running) {
            ui_manager.Update();
            
            // Get device positions and update UI
            auto devices = g_driver->GetDevices();
            ui_manager.UpdateDevicePositions(devices);
            
            // Render the UI
            ui_manager.Render();
            
            // Small sleep to prevent high CPU usage
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60fps
        }
        
        // Cleanup UI
        ui_manager.Shutdown();
    });
    
    // Set thread to detach to allow it to clean up automatically
    ui_thread.detach();
    
    // Return to allow the OpenVR system to manage the driver
    return 0;
}
