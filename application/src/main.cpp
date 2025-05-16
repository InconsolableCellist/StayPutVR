#include <thread>
#include <atomic>
#include <memory>
#include <filesystem>
#include "UI/UIManager.hpp"
#include "../../common/Logger.hpp"
#include "../../common/PathUtils.hpp"
#include "../../common/Audio.hpp"
#include "../../common/Config.hpp"

// Global variables
std::atomic<bool> g_running = true;

// Main entry point when run as a standalone application
int main(int argc, char* argv[]) {
    try {
        // Use AppData path for logging instead of hardcoded SteamVR path
        std::string appDataPath = StayPutVR::GetAppDataPath();
        std::string logPath = appDataPath + "\\logs";
        std::string configPath = appDataPath + "\\config";
        std::string configsPath = appDataPath + "\\configs";
        std::string resourcesPath = appDataPath + "\\resources";
        
        // Create all required directories if they don't exist
        try {
            // Output directory creation status for debugging
            std::cerr << "Creating directories..." << std::endl;
            std::cerr << "AppData path: " << appDataPath << std::endl;
            
            if (!std::filesystem::exists(appDataPath)) {
                std::cerr << "Creating main app directory: " << appDataPath << std::endl;
                std::filesystem::create_directory(appDataPath);
            }
            
            if (!std::filesystem::exists(logPath)) {
                std::cerr << "Creating logs directory: " << logPath << std::endl;
                std::filesystem::create_directory(logPath);
            }
            
            if (!std::filesystem::exists(configPath)) {
                std::cerr << "Creating config directory: " << configPath << std::endl;
                std::filesystem::create_directory(configPath);
            }
            
            if (!std::filesystem::exists(configsPath)) {
                std::cerr << "Creating configs directory: " << configsPath << std::endl;
                std::filesystem::create_directory(configsPath);
            }
            
            if (!std::filesystem::exists(resourcesPath)) {
                std::cerr << "Creating resources directory: " << resourcesPath << std::endl;
                std::filesystem::create_directory(resourcesPath);
                
                // We should check if the app has a local resources directory 
                // and copy warning.wav into the AppData resources folder
                if (std::filesystem::exists("./resources/warning.wav")) {
                    std::cerr << "Copying warning.wav to resources directory" << std::endl;
                    std::filesystem::copy_file(
                        "./resources/warning.wav", 
                        resourcesPath + "/warning.wav",
                        std::filesystem::copy_options::overwrite_existing
                    );
                }
            }
            
            std::cerr << "All directories created successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error creating app directories: " << e.what() << std::endl;
            // Fall back to current directory
            logPath = "./logs";
            configPath = "./config";
            configsPath = "./configs";
            resourcesPath = "./resources";
            
            try {
                std::filesystem::create_directory("./logs");
                std::filesystem::create_directory("./config");
                std::filesystem::create_directory("./configs");
                std::filesystem::create_directory("./resources");
            } catch (const std::exception& e2) {
                std::cerr << "Error creating fallback directories: " << e2.what() << std::endl;
                // Continue anyway - we'll try to run without proper directories
            }
        }
        
        StayPutVR::Logger::Init(logPath, StayPutVR::Logger::LogType::APPLICATION);
        StayPutVR::Logger::Info("StayPutVR application starting up");
        StayPutVR::Logger::Info("Log path: " + logPath);
        StayPutVR::Logger::Info("Current directory: " + std::filesystem::current_path().string());
        
        // Initialize the audio system
        StayPutVR::Logger::Info("Initializing audio system");
        StayPutVR::AudioManager::Initialize();
        
        // Create an instance of the UI manager
        StayPutVR::Logger::Info("Creating UIManager instance");
        StayPutVR::UIManager ui_manager;
        
        // Initialize the UI
        StayPutVR::Logger::Info("Initializing UI");
        if (!ui_manager.Initialize()) {
            // Handle initialization error
            StayPutVR::Logger::Critical("Failed to initialize UI");
            StayPutVR::AudioManager::Shutdown();
            StayPutVR::Logger::Shutdown();
            return 1;
        }
        
        StayPutVR::Logger::Info("Entering main loop");
        // Main loop
        while (g_running) {
            try {
                // Update UI (which will also update the device manager)
                ui_manager.Update();
                
                // Render the UI
                ui_manager.Render();
                
                // Small sleep to prevent high CPU usage
                std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60fps
            }
            catch (const std::exception& e) {
                StayPutVR::Logger::Error("Exception in main loop: " + std::string(e.what()));
                // Continue running despite exceptions
            }
            catch (...) {
                StayPutVR::Logger::Error("Unknown exception in main loop");
                // Continue running despite exceptions
            }
        }
        
        // Cleanup
        StayPutVR::Logger::Info("Shutting down UI");
        ui_manager.Shutdown();
        
        // Shutdown audio system
        StayPutVR::Logger::Info("Shutting down audio system");
        StayPutVR::AudioManager::Shutdown();
        
        StayPutVR::Logger::Info("StayPutVR exiting normally");
        StayPutVR::Logger::Shutdown();
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Critical exception: " << e.what() << std::endl;
        if (StayPutVR::Logger::IsInitialized()) {
            StayPutVR::Logger::Critical("Critical exception: " + std::string(e.what()));
            StayPutVR::Logger::Shutdown();
        }
        return 1;
    }
    catch (...) {
        std::cerr << "Unknown critical exception" << std::endl;
        if (StayPutVR::Logger::IsInitialized()) {
            StayPutVR::Logger::Critical("Unknown critical exception");
            StayPutVR::Logger::Shutdown();
        }
        return 1;
    }
}
