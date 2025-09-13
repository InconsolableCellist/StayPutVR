#include <thread>
#include <atomic>
#include <memory>
#include <filesystem>
// Windows-specific includes
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "resource.h"
#endif
#include "../../common/OSCManager.hpp"
#include "UI/UIManager.hpp"
#include "../../common/Logger.hpp"
#include "../../common/PathUtils.hpp"
#include "../../common/Audio.hpp"
#include "../../common/Config.hpp"
#include "../../common/HttpClient.hpp"

std::atomic<bool> g_running = true;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    try {
        std::string appDataPath = StayPutVR::GetAppDataPath();
        std::string logPath = appDataPath + "\\logs";
        std::string configPath = appDataPath + "\\config";
        std::string resourcesPath = appDataPath + "\\resources";
        
        // Create all required directories if they don't exist
        try {
            if (!std::filesystem::exists(appDataPath)) {
                std::filesystem::create_directory(appDataPath);
            }
            
            if (!std::filesystem::exists(logPath)) {
                std::filesystem::create_directory(logPath);
            }
            
            if (!std::filesystem::exists(configPath)) {
                std::filesystem::create_directory(configPath);
            }
            
            if (!std::filesystem::exists(resourcesPath)) {
                std::filesystem::create_directory(resourcesPath);
                
                // We should check if the app has a local resources directory 
                // and copy warning.wav into the AppData resources folder
                if (std::filesystem::exists("./resources/warning.wav")) {
                    std::filesystem::copy_file(
                        "./resources/warning.wav", 
                        resourcesPath + "/warning.wav",
                        std::filesystem::copy_options::overwrite_existing
                    );
                }
            }
        } catch (const std::exception& e) {
            // Fall back to current directory
            logPath = "./logs";
            configPath = "./config";
            resourcesPath = "./resources";
            
            try {
                std::filesystem::create_directory("./logs");
                std::filesystem::create_directory("./config");
                std::filesystem::create_directory("./resources");
            } catch (const std::exception& e2) {
                // Continue anyway - we'll try to run without proper directories
            }
        }
        
        // Initialize the logger
        StayPutVR::Logger::Init(logPath, StayPutVR::Logger::LogType::APPLICATION);
        StayPutVR::Logger::Info("StayPutVR application starting up");
        StayPutVR::Logger::Info("Log path: " + logPath);
        StayPutVR::Logger::Info("Current directory: " + std::filesystem::current_path().string());
        
        // Load configuration to get log level setting
        StayPutVR::Config config;
        if (config.LoadFromFile("config.ini")) {
            StayPutVR::Logger::LoadLogLevelFromConfig(config);
            StayPutVR::Logger::Info("Loaded log level from config: " + config.log_level);
        }
        
        // Initialize the audio system
        StayPutVR::Logger::Info("Initializing audio system");
        StayPutVR::AudioManager::Initialize();
        
        // Initialize OSC manager if enabled
        if (config.osc_enabled) {
            if (!StayPutVR::OSCManager::GetInstance().Initialize(config.osc_address, config.osc_send_port, config.osc_receive_port)) {
                StayPutVR::Logger::Error("Failed to initialize OSC manager");
                return 1;
            }
        }
        
        // Create an instance of the UI manager
        StayPutVR::Logger::Info("Creating UIManager instance");
        StayPutVR::UIManager ui_manager;
        
        // Initialize the UI
        StayPutVR::Logger::Info("Initializing UI");
        if (!ui_manager.Initialize()) {
            // Handle initialization error
            StayPutVR::Logger::Critical("Failed to initialize UI");
            StayPutVR::HttpClient::Shutdown();
            StayPutVR::AudioManager::Shutdown();
            StayPutVR::Logger::Shutdown();
            return 1;
        }
        
        StayPutVR::Logger::Info("Entering main loop");
        while (g_running) {
            try {
                // Update UI (which will also update the device manager)
                ui_manager.Update();
                ui_manager.Render();
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
        
        StayPutVR::Logger::Info("Shutting down HttpClient");
        StayPutVR::HttpClient::Shutdown();
        
        StayPutVR::Logger::Info("Shutting down audio system");
        StayPutVR::AudioManager::Shutdown();
        
        if (config.osc_enabled) {
            StayPutVR::OSCManager::GetInstance().Shutdown();
        }
        
        StayPutVR::Logger::Info("StayPutVR exiting normally");
        StayPutVR::Logger::Shutdown();
        return 0;
    }
    catch (const std::exception& e) {
        if (StayPutVR::Logger::IsInitialized()) {
            StayPutVR::Logger::Critical("Critical exception: " + std::string(e.what()));
            StayPutVR::Logger::Shutdown();
        }
        return 1;
    }
    catch (...) {
        if (StayPutVR::Logger::IsInitialized()) {
            StayPutVR::Logger::Critical("Unknown critical exception");
            StayPutVR::Logger::Shutdown();
        }
        return 1;
    }
}
