#include "UIManager.hpp"
#include <iostream>
#include <string>
#include <format>
#include <algorithm>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <nlohmann/json.hpp>
#include "../../common/Logger.hpp"
#include "../../common/PathUtils.hpp"
#include "../../common/Audio.hpp"
#include <shellapi.h> // For ShellExecuteA
#include <thread> // For std::this_thread::sleep_for
#include "../../../common/OSCManager.hpp"
#include "../../common/HttpClient.hpp"

// Windows-specific includes for icon handling
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include "../resource.h"
#endif

using json = nlohmann::json;

extern std::atomic<bool> g_running;

namespace StayPutVR {

    UIManager::UIManager() : window_(nullptr), imgui_context_(nullptr), running_ptr_(&g_running) {
        // Initialize config_dir_ with AppData path
        config_dir_ = GetAppDataPath() + "\\config";
        // Initialize config_file_ with just the filename, not the full path
        config_file_ = "config.ini";
        // Increase window height to prevent cutting off UI elements
        window_height_ = 750;
        
        // Create device manager instance
        device_manager_ = new DeviceManager();
        
        // Initialize timestamps
        last_sound_time_ = std::chrono::steady_clock::now();
        last_osc_toggle_time_ = std::chrono::steady_clock::now();
    }

    UIManager::~UIManager() {
        Shutdown();
        
        ShutdownTwitchManager();
        ShutdownPiShockManager();
        ShutdownOpenShockManager();
        
        if (device_manager_) {
            delete device_manager_;
            device_manager_ = nullptr;
        }
    }

    bool UIManager::Initialize() {
        glfwSetErrorCallback(UIManager::GlfwErrorCallback);
        
        if (!glfwInit()) {
            std::cerr << "Failed to initialize GLFW" << std::endl;
            return false;
        }
        
        // GL 3.0 + GLSL 130
        const char* glsl_version = "#version 130";
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        
        // Create window with graphics context
        window_ = glfwCreateWindow(window_width_, window_height_, window_title_.c_str(), nullptr, nullptr);
        if (window_ == nullptr) {
            std::cerr << "Failed to create GLFW window" << std::endl;
            return false;
        }
        
#ifdef _WIN32
        // Set window icon
        HWND hwnd = glfwGetWin32Window(window_);
        if (hwnd) {
            HICON hIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_ICON1));
            if (hIcon) {
                SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
                SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
            }
        }
#endif
        
        glfwMakeContextCurrent(window_);
        glfwSwapInterval(1); // Enable vsync
        
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cerr << "Failed to initialize GLAD" << std::endl;
            return false;
        }
        
        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        imgui_context_ = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
        
        ImGui::StyleColorsDark();
        
        // Setup Platform/Renderer backends
        ImGui_ImplGlfw_InitForOpenGL(window_, true);
        ImGui_ImplOpenGL3_Init(glsl_version);
        
        // Set window close callback
        glfwSetWindowCloseCallback(window_, [](GLFWwindow* window) {
            g_running = false;
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Info("Window close callback triggered");
            }
        });
        
        // Create all necessary directories
        std::string appDataPath = GetAppDataPath();
        std::string logPath = appDataPath + "\\logs";
        std::string configPath = appDataPath + "\\config";
        std::string resourcesPath = appDataPath + "\\resources";
        
        try {
            std::filesystem::create_directories(logPath);
            std::filesystem::create_directories(configPath);
            std::filesystem::create_directories(resourcesPath);
            
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Info("Created directories if needed: " + logPath);
            }
        } catch (const std::exception& e) {
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Error("Failed to create directories: " + std::string(e.what()));
            }
        }
        
        // Create config directories
        std::filesystem::create_directories(config_dir_);
        std::filesystem::create_directories(GetAppDataPath() + "\\config");
        
        // Initialize audio system
        AudioManager::Initialize();
        
        // Load configuration
        LoadConfig();
        
        // Automatically connect to OSC if it was previously enabled
        if (config_.osc_enabled) {
            if (Logger::IsInitialized()) {
                Logger::Info("UIManager: OSC was previously enabled, connecting automatically");
            }
            
            // Add a small delay to ensure all components are properly initialized
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Try to initialize OSC
            bool osc_init_result = OSCManager::GetInstance().Initialize(config_.osc_address, config_.osc_send_port, config_.osc_receive_port);
            
            if (osc_init_result) {
                osc_enabled_ = true;
                
                // Configure OSC paths
                OSCManager::GetInstance().SetConfig(config_);
                
                // Explicitly set up callbacks
                OSCManager::GetInstance().SetLockCallback(
                    [this](OSCDeviceType device, bool locked) {
                        OnDeviceLocked(device, locked);
                    }
                );
                
                OSCManager::GetInstance().SetIncludeCallback(
                    [this](OSCDeviceType device, bool include) {
                        OnDeviceIncluded(device, include);
                    }
                );
                
                // Set up OSC callbacks in Initialize() method
                OSCManager::GetInstance().SetGlobalLockCallback(
                    [this](bool lock) {
                        if (Logger::IsInitialized()) {
                            Logger::Info("Global " + std::string(lock ? "lock" : "unlock") + " triggered via OSC");
                        }
                        // Use the same method the UI uses, which handles countdown and sound effects
                        ActivateGlobalLock(lock);
                    }
                );
                
                OSCManager::GetInstance().SetGlobalOutOfBoundsCallback(
                    [this](bool triggered) {
                        if (!config_.osc_global_out_of_bounds_enabled) {
                            return;
                        }
                        if (Logger::IsInitialized()) {
                            Logger::Info("Global out-of-bounds triggered via OSC");
                        }
                        TriggerGlobalOutOfBoundsActions();
                    }
                );
                
                OSCManager::GetInstance().SetBiteCallback(
                    [this](bool triggered) {
                        if (!config_.osc_bite_enabled) {
                            return;
                        }
                        if (Logger::IsInitialized()) {
                            Logger::Info("Bite triggered via OSC");
                        }
                        TriggerBiteActions();
                    }
                );
                
                OSCManager::GetInstance().SetEStopStretchCallback(
                    [this](float stretch_value) {
                        if (!config_.osc_estop_stretch_enabled) {
                            return;
                        }
                        if (Logger::IsInitialized()) {
                            Logger::Info("Emergency stop stretch triggered via OSC with value: " + std::to_string(stretch_value) + " - entering emergency stop mode");
                        }
                        
                        // Enter emergency stop mode
                        emergency_stop_active_ = true;
                        
                        // Unlock all devices immediately
                        ActivateGlobalLock(false);
                        
                        // Also unlock any individually locked devices
                        for (auto& device : device_positions_) {
                            if (device.locked) {
                                LockDevicePosition(device.serial, false);
                            }
                        }
                        
                        if (Logger::IsInitialized()) {
                            Logger::Warning("EMERGENCY STOP MODE ACTIVE - All actions disabled until reset");
                        }
                    }
                );
                
                if (Logger::IsInitialized()) {
                    Logger::Info("UIManager: OSC auto-connection successful, callbacks registered");
                }
            } else {
                if (Logger::IsInitialized()) {
                    Logger::Error("UIManager: OSC auto-connection failed, will need manual activation");
                }
                osc_enabled_ = false;
                config_.osc_enabled = false;
                SaveConfig();
            }
        }
        
        // Initialize device manager - but continue even if it fails
        if (device_manager_) {
            if (!device_manager_->Initialize()) {
                if (StayPutVR::Logger::IsInitialized()) {
                    StayPutVR::Logger::Warning("Failed to connect to driver IPC server - continuing without device connection");
                }
                // Don't return false here, just continue with the UI
            }
        }
        
        InitializeTwitchManager();
        InitializePiShockManager();
        InitializePiShockWebSocketManager();
        InitializeOpenShockManager();
        
        return true;
    }

    void UIManager::Update() {
        // Poll and handle events
        glfwPollEvents();
        
        // Check if window should close
        if (glfwWindowShouldClose(window_)) {
            *running_ptr_ = false;
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Info("Window close button pressed, shutting down application");
            }
            glfwSetWindowShouldClose(window_, GLFW_FALSE); // Reset the flag
        }
        
        // Update countdown timer if active
        if (countdown_active_) {
            auto current_time = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - countdown_last_beep_).count();
            
            // Check if countdown has finished (3 seconds)
            if (elapsed >= countdown_remaining_) {
                countdown_active_ = false;
                if (StayPutVR::Logger::IsInitialized()) {
                    StayPutVR::Logger::Info("Countdown finished, activating lock");
                }
                ActivateGlobalLockInternal(true);
            }
        }
        
        // Update Twitch manager
        if (twitch_manager_) {
            twitch_manager_->Update();
        }
        
        // Update PiShock WebSocket manager
        if (pishock_ws_manager_) {
            pishock_ws_manager_->Update();
        }
        
        // Process Twitch unlock timer
        ProcessTwitchUnlockTimer();
        
        // Process global out-of-bounds timer
        ProcessGlobalOutOfBoundsTimer();
        ProcessBiteTimer();
        
        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        
        // Update device manager
        if (device_manager_) {
            device_manager_->Update();
            
            // Get updated device positions
            const auto& devices = device_manager_->GetDevices();
            
            // Process device positions for UI
            UpdateDevicePositions(devices);
        }
    }

    void UIManager::Render() {
        // Render the UI
        RenderMainWindow();
        
        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window_, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        
        glfwSwapBuffers(window_);
    }

    void UIManager::Shutdown() {
        if (StayPutVR::Logger::IsInitialized()) {
            StayPutVR::Logger::Info("UIManager shutting down");
        }
        
        // Save configuration before shutting down
        SaveConfig();
        
        // Shutdown managers
        ShutdownTwitchManager();
        ShutdownPiShockManager();
        ShutdownOpenShockManager();
        
        // Give managers time to properly clean up (especially WebSocket connections)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // Shutdown device manager and IPC connection
        if (device_manager_) {
            Logger::Info("UIManager: Shutting down device manager");
            try {
                device_manager_->Shutdown();
                // Give some time for the IPC connection to properly close
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            catch (const std::exception& e) {
                Logger::Error("Exception when shutting down device manager: " + std::string(e.what()));
            }
        }
        
        // Shutdown audio system
        AudioManager::Shutdown();
        
        if (window_ != nullptr) {
            // Cleanup
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext(imgui_context_);
            
            glfwDestroyWindow(window_);
            glfwTerminate();
            
            window_ = nullptr;
            imgui_context_ = nullptr;
        }
    }

    void UIManager::UpdateDevicePositions(const std::vector<DevicePositionData>& devices) {
        // Mark current time for tracking device activity
        auto now = std::chrono::steady_clock::now();
        
        // Create a set of all current device serials
        std::unordered_set<std::string> current_device_serials;
        
        // Update device map
        for (const auto& device : devices) {
            std::string serial = device.serial;
            current_device_serials.insert(serial);
            
            // Check if this device exists in our map
            auto it = device_map_.find(serial);
            
            if (it == device_map_.end()) {
                // New device, add it
                DevicePosition pos;
                pos.serial = serial;
                pos.type = device.type;
                
                // Store position
                pos.position[0] = device.position[0];
                pos.position[1] = device.position[1];
                pos.position[2] = device.position[2];
                
                // Store rotation
                pos.rotation[0] = device.rotation[0];
                pos.rotation[1] = device.rotation[1];
                pos.rotation[2] = device.rotation[2];
                pos.rotation[3] = device.rotation[3];
                
                // Initialize original position and rotation with current values
                for (int i = 0; i < 3; i++) {
                    pos.original_position[i] = pos.position[i];
                    pos.previous_position[i] = pos.position[i];
                }
                for (int i = 0; i < 4; i++) pos.original_rotation[i] = pos.rotation[i];
                
                // Initialize last update time
                pos.last_update_time = now;
                
                // Look up device name from config if available
                auto nameIt = config_.device_names.find(serial);
                if (nameIt != config_.device_names.end()) {
                    pos.device_name = nameIt->second;
                    if (StayPutVR::Logger::IsInitialized()) {
                        StayPutVR::Logger::Info("Applied stored name for device: " + serial + " -> " + nameIt->second);
                    }
                }
                
                // Apply include_in_locking setting if available
                auto settingIt = config_.device_settings.find(serial);
                if (settingIt != config_.device_settings.end()) {
                    pos.include_in_locking = settingIt->second;
                    if (StayPutVR::Logger::IsInitialized()) {
                        StayPutVR::Logger::Info("Applied include_in_locking setting for device: " + serial + " -> " + 
                                                (pos.include_in_locking ? "true" : "false"));
                    }
                }
                
                // Apply device role if available in config
                auto roleIt = config_.device_roles.find(serial);
                if (roleIt != config_.device_roles.end()) {
                    int roleValue = roleIt->second;
                    pos.role = static_cast<DeviceRole>(roleValue);
                    
                    if (StayPutVR::Logger::IsInitialized()) {
                        std::string roleName = "Unknown";
                        switch (pos.role) {
                            case DeviceRole::None: roleName = "None"; break;
                            case DeviceRole::HMD: roleName = "HMD"; break;
                            case DeviceRole::LeftController: roleName = "LeftController"; break;
                            case DeviceRole::RightController: roleName = "RightController"; break;
                            case DeviceRole::Hip: roleName = "Hip"; break;
                            case DeviceRole::LeftFoot: roleName = "LeftFoot"; break;
                            case DeviceRole::RightFoot: roleName = "RightFoot"; break;
                            default: roleName = "Unknown"; break;
                        }
                        StayPutVR::Logger::Info("Applied stored role for device: " + serial + " -> " + roleName + 
                                              " (value: " + std::to_string(roleValue) + ")");
                    }
                }
                
                // Log new device
                if (StayPutVR::Logger::IsInitialized()) {
                    StayPutVR::Logger::Info("New device connected: " + serial);
                }
                
                // Add to our list and map
                device_positions_.push_back(pos);
                device_map_[serial] = device_positions_.size() - 1;
            } else {
                // Existing device, update it
                size_t index = it->second;
                
                // Save previous position
                for (int i = 0; i < 3; i++) {
                    device_positions_[index].previous_position[i] = device_positions_[index].position[i];
                }
                
                // Store current position
                float current_pos[3] = {
                    device.position[0],
                    device.position[1],
                    device.position[2]
                };
                float current_rot[4] = {
                    device.rotation[0],
                    device.rotation[1],
                    device.rotation[2],
                    device.rotation[3]
                };
                
                // Update position and rotation
                for (int i = 0; i < 3; i++) {
                    device_positions_[index].position[i] = current_pos[i];
                }
                for (int i = 0; i < 4; i++) {
                    device_positions_[index].rotation[i] = current_rot[i];
                }
                
                if (device_positions_[index].locked) {
                    // If locked, calculate and store position offset
                    for (int i = 0; i < 3; i++) {
                        device_positions_[index].position_offset[i] = 
                            device_positions_[index].original_position[i] - current_pos[i];
                    }
                    
                    // For quaternions, we should use proper quaternion math for offsets
                    // This is simplified for now
                    device_positions_[index].rotation_offset[0] = current_rot[0];
                    device_positions_[index].rotation_offset[1] = current_rot[1];
                    device_positions_[index].rotation_offset[2] = current_rot[2];
                    device_positions_[index].rotation_offset[3] = current_rot[3];
                }
                
                // Update last update time
                device_positions_[index].last_update_time = now;
            }
        }
        
        // Check for devices that need to be removed from the list
        // (they haven't been seen for more than 5 seconds)
        std::vector<size_t> indices_to_remove;
        std::vector<std::string> serials_to_remove;
        
        for (size_t i = 0; i < device_positions_.size(); ++i) {
            const auto& device = device_positions_[i];
            
            // Skip devices that are in the current update
            if (current_device_serials.find(device.serial) != current_device_serials.end()) {
                continue;
            }
            
            // If device hasn't been updated in more than 5 seconds, mark for removal
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - device.last_update_time).count();
            if (elapsed > 5) {
                indices_to_remove.push_back(i);
                serials_to_remove.push_back(device.serial);
                
                if (StayPutVR::Logger::IsInitialized()) {
                    StayPutVR::Logger::Info("Device disconnected: " + device.serial);
                }
            }
        }
        
        // Remove devices that are no longer present
        if (!indices_to_remove.empty()) {
            // Remove from device_map
            for (const auto& serial : serials_to_remove) {
                device_map_.erase(serial);
            }
            
            // Remove from device_positions_ in reverse order to maintain valid indices
            std::sort(indices_to_remove.begin(), indices_to_remove.end(), std::greater<size_t>());
            for (size_t index : indices_to_remove) {
                device_positions_.erase(device_positions_.begin() + index);
            }
            
            // Rebuild device_map with updated indices
            device_map_.clear();
            for (size_t i = 0; i < device_positions_.size(); ++i) {
                device_map_[device_positions_[i].serial] = i;
            }
        }
        
        // Check position deviations if global lock is active
        if (global_lock_active_) {
            CheckDevicePositionDeviations();
        }
        // Also check if there are any individually locked devices
        else {
            bool has_locked_devices = false;
            for (const auto& device : device_positions_) {
                if (device.locked) {
                    has_locked_devices = true;
                    break;
                }
            }
            
            if (has_locked_devices) {
                CheckDevicePositionDeviations();
            }
        }

        // Save device names to configuration if they exist
        bool names_changed = false;
        for (const auto& device : device_positions_) {
            if (!device.device_name.empty()) {
                if (config_.device_names[device.serial] != device.device_name) {
                    config_.device_names[device.serial] = device.device_name;
                    names_changed = true;
                }
            }
        }

        // Save config if device names have changed
        if (names_changed) {
            SaveConfig();
        }
    }
    
    void UIManager::LockDevicePosition(const std::string& serial, bool lock) {
        // Prevent locking during emergency stop mode (but allow unlocking)
        if (emergency_stop_active_ && lock) {
            if (Logger::IsInitialized()) {
                Logger::Warning("Cannot lock device " + serial + " - emergency stop mode is active");
            }
            return;
        }
        
        auto it = device_map_.find(serial);
        if (it != device_map_.end()) {
            size_t index = it->second;
            device_positions_[index].locked = lock;
            
            // If locking, store the current position as original
            if (lock) {
                for (int i = 0; i < 3; i++) device_positions_[index].original_position[i] = device_positions_[index].position[i];
                for (int i = 0; i < 4; i++) device_positions_[index].original_rotation[i] = device_positions_[index].rotation[i];
                // Initialize deviation tracking
                device_positions_[index].position_deviation = 0.0f;
                device_positions_[index].exceeds_threshold = false;
                device_positions_[index].in_warning_zone = false;
            }
            
            // Send OSC status update for individual device lock/unlock
            if (device_positions_[index].role != DeviceRole::None) {
                OSCDeviceType oscDevice = DeviceRoleToOSCDeviceType(device_positions_[index].role);
                DeviceStatus status = lock ? DeviceStatus::LockedSafe : DeviceStatus::Unlocked;
                UpdateDeviceStatus(oscDevice, status);
                
                if (StayPutVR::Logger::IsInitialized()) {
                    StayPutVR::Logger::Debug("Sent OSC status " + std::string(lock ? "LockedSafe" : "Unlocked") + 
                                           " for manually " + std::string(lock ? "locked" : "unlocked") + 
                                           " device " + serial + " (role: " + 
                                           OSCManager::GetInstance().GetRoleString(device_positions_[index].role) + ")");
                }
            }
        }
    }
    
    void UIManager::ActivateGlobalLock(bool activate) {
        if (activate && config_.countdown_enabled) {
            // Start countdown by playing countdown.wav once
            // The countdown.wav is a 3-second sound
            if (config_.audio_enabled) {
                std::string filePath = StayPutVR::GetAppDataPath() + "\\resources\\countdown.wav";
                if (std::filesystem::exists(filePath)) {
                    // Set timeout for the lock activation
                    countdown_active_ = true;
                    countdown_remaining_ = 3.5f; // 3.5 seconds to account for audio clip ending
                    countdown_last_beep_ = std::chrono::steady_clock::now();
                    
                    if (StayPutVR::Logger::IsInitialized()) {
                        StayPutVR::Logger::Info("Starting countdown with countdown sound");
                    }
                    
                    // Play the countdown sound once
                    AudioManager::PlaySound("countdown.wav", config_.audio_volume);
                } else {
                    // If countdown.wav doesn't exist, activate lock immediately
                    if (StayPutVR::Logger::IsInitialized()) {
                        StayPutVR::Logger::Warning("countdown.wav not found, locking immediately");
                    }
                    ActivateGlobalLockInternal(true);
                }
            } else {
                // Audio disabled, activate lock immediately
                ActivateGlobalLockInternal(true);
            }
            return; // Don't activate global lock yet, wait for countdown
        }
        
        // Direct activation/deactivation without countdown
        ActivateGlobalLockInternal(activate);
    }
    
    // Internal method to actually handle the lock activation
    void UIManager::ActivateGlobalLockInternal(bool activate) {
        // Prevent locking during emergency stop mode (but allow unlocking)
        if (emergency_stop_active_ && activate) {
            if (Logger::IsInitialized()) {
                Logger::Warning("Cannot activate global lock - emergency stop mode is active");
            }
            return;
        }
        
        global_lock_active_ = activate;
        
        // If activating, store current positions as original for all included devices
        if (activate) {
            for (auto& device : device_positions_) {
                if (device.include_in_locking) {
                    for (int i = 0; i < 3; i++) device.original_position[i] = device.position[i];
                    for (int i = 0; i < 4; i++) device.original_rotation[i] = device.rotation[i];
                    device.position_deviation = 0.0f;
                    device.exceeds_threshold = false;
                    
                    // Send OSC status update for global lock
                    if (device.role != DeviceRole::None) {
                        OSCDeviceType oscDevice = DeviceRoleToOSCDeviceType(device.role);
                        UpdateDeviceStatus(oscDevice, DeviceStatus::LockedSafe);
                        
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Debug("Sent OSC status LockedSafe for globally locked device " + 
                                                   device.serial + " (role: " + 
                                                   OSCManager::GetInstance().GetRoleString(device.role) + ")");
                        }
                    }
                }
            }
            
            // Play lock sound if enabled
            if (config_.audio_enabled && config_.lock_audio) {
                AudioManager::PlayLockSound(config_.audio_volume);
            }
        } else {
            // Send OSC status updates for global unlock
            for (auto& device : device_positions_) {
                if (device.include_in_locking) {
                    // Send OSC status update for global unlock
                    if (device.role != DeviceRole::None) {
                        OSCDeviceType oscDevice = DeviceRoleToOSCDeviceType(device.role);
                        UpdateDeviceStatus(oscDevice, DeviceStatus::Unlocked);
                        
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Debug("Sent OSC status Unlocked for globally unlocked device " + 
                                                   device.serial + " (role: " + 
                                                   OSCManager::GetInstance().GetRoleString(device.role) + ")");
                        }
                    }
                }
            }
            
            // Play unlock sound if enabled
            if (config_.audio_enabled && config_.unlock_audio) {
                AudioManager::PlayUnlockSound(config_.audio_volume);
            }
        }
    }
    
    void UIManager::CheckDevicePositionDeviations() {
        // Skip all position checking and actions if in emergency stop mode
        if (emergency_stop_active_) {
            return;
        }
        
        bool warning_triggered = false;
        bool out_of_bounds_triggered = false;
        bool success_triggered = false;
        bool disable_threshold_exceeded = false;
        
        // Get current time to check if we should play sound again
        auto current_time = std::chrono::steady_clock::now();
        
        for (auto& device : device_positions_) {
            // Check both globally locked devices AND individually locked devices
            if ((device.include_in_locking && global_lock_active_) || device.locked) {
                // Calculate Euclidean distance between current position and original position
                float deviation = 0.0f;
                for (int i = 0; i < 3; i++) {
                    float diff = device.position[i] - device.original_position[i];
                    deviation += diff * diff;
                }
                device.position_deviation = std::sqrt(deviation);
                
                // Store previous state to detect transitions
                bool was_exceeding = device.exceeds_threshold;
                bool was_warning = device.in_warning_zone;
                
                // Previous zone status - safe zone is when not in warning or exceeding
                bool was_in_safe_zone = !was_exceeding && !was_warning;
                
                // Check if device is beyond the disable threshold
                bool beyond_disable_threshold = device.position_deviation > disable_threshold_;
                
                // If any device exceeds the disable threshold, we'll skip all alerts
                if (beyond_disable_threshold) {
                    disable_threshold_exceeded = true;
                    
                    if (Logger::IsInitialized()) {
                        Logger::Debug("Device " + device.serial + " exceeded disable threshold: " + 
                                    std::to_string(device.position_deviation) + " > " + std::to_string(disable_threshold_));
                    }
                    
                    // Don't update zone status for devices beyond disable threshold
                    continue;
                }
                
                // Check if deviation exceeds threshold
                device.exceeds_threshold = device.position_deviation > position_threshold_;
                device.in_warning_zone = device.position_deviation > warning_threshold_ && !device.exceeds_threshold;
                
                if (Logger::IsInitialized() && (device.exceeds_threshold || device.in_warning_zone)) {
                    Logger::Debug("Device " + device.serial + " position: deviation=" + std::to_string(device.position_deviation) + 
                                ", warning_threshold=" + std::to_string(warning_threshold_) + 
                                ", position_threshold=" + std::to_string(position_threshold_) + 
                                ", in_warning=" + std::to_string(device.in_warning_zone) + 
                                ", exceeds_threshold=" + std::to_string(device.exceeds_threshold));
                }
                
                // Current zone status - safe zone is when not in warning or exceeding
                bool is_in_safe_zone = !device.exceeds_threshold && !device.in_warning_zone;
                
                // For out of bounds, check continuous presence
                if (device.exceeds_threshold) {
                    out_of_bounds_triggered = true;
                }
                // For warning zone, check continuous presence
                else if (device.in_warning_zone) {
                    warning_triggered = true;
                }
                // Check for transition from warning/exceeding to safe zone
                if (!was_in_safe_zone && is_in_safe_zone) {
                    if (StayPutVR::Logger::IsInitialized()) {
                        StayPutVR::Logger::Debug("Device " + device.serial + " returned to safe zone, triggering success sound");
                    }
                    
                    // Send OSC status update for return to safe zone
                    if (device.role != DeviceRole::None) {
                        OSCDeviceType oscDevice = DeviceRoleToOSCDeviceType(device.role);
                        UpdateDeviceStatus(oscDevice, DeviceStatus::LockedSafe);
                        
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Debug("Sent OSC status LockedSafe for device " + device.serial + 
                                                   " (role: " + OSCManager::GetInstance().GetRoleString(device.role) + ")");
                        }
                    }
                    
                    success_triggered = true;
                }
                
                // Check for newly triggered PiShock events
                if (!was_warning && device.in_warning_zone) {
                    // Newly entered warning zone from safe zone
                    if (StayPutVR::Logger::IsInitialized()) {
                        StayPutVR::Logger::Debug("Device " + device.serial + " entered warning zone, position deviation: " + 
                                                std::to_string(device.position_deviation));
                    }
                    
                    // Remove PiShock warning call - only use audio warnings for this zone
                    // Audio warning will still be handled by the existing code below
                    
                    // Send OSC status update for warning zone entry
                    if (device.role != DeviceRole::None) {
                        OSCDeviceType oscDevice = DeviceRoleToOSCDeviceType(device.role);
                        UpdateDeviceStatus(oscDevice, DeviceStatus::LockedWarning);
                        
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Debug("Sent OSC status LockedWarning for device " + device.serial + 
                                                   " (role: " + OSCManager::GetInstance().GetRoleString(device.role) + ")");
                        }
                    }
                }
                
                // Check for transition from out of bounds back to warning zone
                if (was_exceeding && !device.exceeds_threshold && device.in_warning_zone) {
                    // Device moved back from out of bounds to warning zone
                    if (StayPutVR::Logger::IsInitialized()) {
                        StayPutVR::Logger::Debug("Device " + device.serial + " returned to warning zone from out of bounds, position deviation: " + 
                                                std::to_string(device.position_deviation));
                    }
                    
                    // Send OSC status update for warning zone
                    if (device.role != DeviceRole::None) {
                        OSCDeviceType oscDevice = DeviceRoleToOSCDeviceType(device.role);
                        UpdateDeviceStatus(oscDevice, DeviceStatus::LockedWarning);
                        
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Debug("Sent OSC status LockedWarning for device " + device.serial + 
                                                   " returning from out of bounds (role: " + 
                                                   OSCManager::GetInstance().GetRoleString(device.role) + ")");
                        }
                    }
                }
                
                if (!was_exceeding && device.exceeds_threshold) {
                    // Newly entered out of bounds zone
                    if (StayPutVR::Logger::IsInitialized()) {
                        StayPutVR::Logger::Debug("Device " + device.serial + " entered out of bounds zone, position deviation: " + 
                                                std::to_string(device.position_deviation) + 
                                                ", was_exceeding=" + std::to_string(was_exceeding) + 
                                                ", pishock_enabled=" + std::to_string(config_.pishock_enabled));
                    }
                    
                    // Send OSC status update for disobedience (out of bounds) state
                    if (device.role != DeviceRole::None) {
                        OSCDeviceType oscDevice = DeviceRoleToOSCDeviceType(device.role);
                        UpdateDeviceStatus(oscDevice, DeviceStatus::LockedDisobedience);
                        
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Debug("Sent OSC status LockedDisobedience for device " + device.serial + 
                                                   " (role: " + OSCManager::GetInstance().GetRoleString(device.role) + ")");
                        }
                    }
                    
                    if (StayPutVR::Logger::IsInitialized()) {
                        Logger::Info("Triggering initial PiShock disobedience actions for device " + device.serial);
                    }
                    TriggerPiShockDisobedience(device.serial);
                    
                    if (openshock_manager_ && openshock_manager_->IsEnabled()) {
                        if (StayPutVR::Logger::IsInitialized()) {
                            Logger::Info("Triggering initial OpenShock disobedience actions for device " + device.serial);
                        }
                        openshock_manager_->TriggerDisobedienceActions(device.serial);
                    }
                } 
                // Continue triggering PiShock for devices that remain in out-of-bounds zone
                else if (device.exceeds_threshold && CanTriggerPiShock()) {
                    if (StayPutVR::Logger::IsInitialized()) {
                        Logger::Info("Triggering continuous PiShock disobedience actions for device " + device.serial);
                    }
                    TriggerPiShockDisobedience(device.serial);
                }
                
                // Continue triggering OpenShock for devices that remain in out-of-bounds zone
                else if (device.exceeds_threshold && openshock_manager_ && openshock_manager_->IsEnabled()) {
                    // OpenShockManager handles its own rate limiting
                    if (openshock_manager_->CanTriggerAction()) {
                        if (StayPutVR::Logger::IsInitialized()) {
                            Logger::Info("Triggering continuous OpenShock disobedience actions for device " + device.serial);
                        }
                        openshock_manager_->TriggerDisobedienceActions(device.serial);
                    }
                }
            }
        }
        
        // If any device exceeded the disable threshold, skip all alerts
        if (disable_threshold_exceeded) {
            if (Logger::IsInitialized()) {
                Logger::Debug("Disable threshold exceeded, skipping all audio and PiShock alerts");
            }
            return;
        }
        
        // Sound rate limiting - use longer cooldown to prevent audio from being cut off
        // Increased from 0.5 to 1.0 seconds to ensure audio files have time to play fully
        const float sound_cooldown_seconds = 1.0f;
        
        // Play sounds if needed and if audio is enabled in config
        if (config_.audio_enabled) {
            // Special case for success sound: Always play immediately when triggered, 
            // regardless of cooldown or other playing sounds
            if (success_triggered) {
                std::string filePath = StayPutVR::GetAppDataPath() + "\\resources\\success.wav";
                if (std::filesystem::exists(filePath)) {
                    if (StayPutVR::Logger::IsInitialized()) {
                        StayPutVR::Logger::Debug("Playing success.wav for return to safe zone");
                    }
                    // Stop any currently playing sound first
                    AudioManager::StopSound();
                    // Play the success sound with the current volume setting
                    AudioManager::PlaySound("success.wav", config_.audio_volume);
                    // Reset the cooldown timer
                    last_sound_time_ = current_time;
                    return; // Skip other sounds this frame
                } else {
                    if (StayPutVR::Logger::IsInitialized()) {
                        StayPutVR::Logger::Warning("success.wav not found, cannot play safe zone return sound");
                    }
                }
            }
            
            // For other sounds, respect the cooldown
            auto elapsed_seconds = std::chrono::duration_cast<std::chrono::duration<float>>(
                current_time - last_sound_time_).count();
                
            if (elapsed_seconds >= sound_cooldown_seconds) {
                bool played_sound = false;
                
                // Out of bounds sound (disobedience.wav)
                if (out_of_bounds_triggered && config_.out_of_bounds_audio) {
                    std::string filePath = StayPutVR::GetAppDataPath() + "\\resources\\disobedience.wav";
                    if (std::filesystem::exists(filePath)) {
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Debug("Playing disobedience.wav for out of bounds");
                        }
                        AudioManager::PlaySound("disobedience.wav", config_.audio_volume);
                        played_sound = true;
                    } else {
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Warning("disobedience.wav not found, cannot play out of bounds sound");
                        }
                    }
                }
                // Warning sound
                else if (warning_triggered && config_.warning_audio) {
                    AudioManager::PlayWarningSound(config_.audio_volume);
                    played_sound = true;
                }
                
                // If we played a sound, update the timestamp
                if (played_sound) {
                    last_sound_time_ = current_time;
                }
            }
        }
    }
    
    void UIManager::ResetAllDevices() {
        // Reset global lock state
        global_lock_active_ = false;
        
        // Reset individual device states
        for (auto& device : device_positions_) {
            device.locked = false;
            device.include_in_locking = false;
            device.exceeds_threshold = false;
            device.position_deviation = 0.0f;
            
            for (int i = 0; i < 3; i++) device.position_offset[i] = 0.0f;
            device.rotation_offset[0] = 0.0f;
            device.rotation_offset[1] = 0.0f;
            device.rotation_offset[2] = 0.0f;
            device.rotation_offset[3] = 1.0f; // Identity quaternion
        }
    }
    
    void UIManager::ApplyLockedPositions() {
        // This would apply offsets to devices in a full implementation
        // For now, this is just a placeholder
    }
    
    bool UIManager::SaveDevicePositions(const std::string& filename) {
        json config;
        
        // Ensure the config directory exists
        std::filesystem::create_directories(config_dir_);
        
        // Create an array of device positions
        json devices_array = json::array();
        
        for (const auto& device : device_positions_) {
            if (device.locked || device.include_in_locking) {
                json device_obj;
                device_obj["serial"] = device.serial;
                device_obj["locked"] = device.locked;
                device_obj["include_in_locking"] = device.include_in_locking;
                device_obj["role"] = static_cast<int>(device.role);
                
                // Save device name if it's set
                if (!device.device_name.empty()) {
                    device_obj["device_name"] = device.device_name;
                }
                
                json position_array = json::array();
                for (int i = 0; i < 3; i++) position_array.push_back(device.original_position[i]);
                device_obj["position"] = position_array;
                
                json rotation_array = json::array();
                for (int i = 0; i < 4; i++) rotation_array.push_back(device.original_rotation[i]);
                device_obj["rotation"] = rotation_array;
                
                devices_array.push_back(device_obj);
            }
        }
        
        config["devices"] = devices_array;
        config["position_threshold"] = position_threshold_;
        config["warning_threshold"] = warning_threshold_;
        config["disable_threshold"] = disable_threshold_;
        
        // Save to file
        std::string filepath = config_dir_ + "\\" + filename + ".json";
        std::ofstream out(filepath);
        if (!out.is_open()) {
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Error("UIManager: Failed to open file for writing: " + filepath);
            }
            return false;
        }
        
        out << config.dump(4); // Pretty print with 4 spaces indent
        out.close();
        
        if (StayPutVR::Logger::IsInitialized()) {
            StayPutVR::Logger::Info("UIManager: Saved device positions to " + filepath);
        }
        
        current_config_file_ = filename;
        return true;
    }
    
    bool UIManager::LoadDevicePositions(const std::string& filename) {
        // Reset all devices first
        ResetAllDevices();
        
        // Ensure the config directory exists
        std::filesystem::create_directories(config_dir_);
        
        // Load the config file
        std::string filepath = config_dir_ + "\\" + filename + ".json";
        std::ifstream in(filepath);
        if (!in.is_open()) {
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Error("UIManager: Failed to open file for reading: " + filepath);
            }
            return false;
        }
        
        json config;
        try {
            in >> config;
        } catch (const std::exception& e) {
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Error("UIManager: Error parsing config file: " + std::string(e.what()));
            }
            return false;
        }
        
        // Load position threshold if available
        if (config.contains("position_threshold") && config["position_threshold"].is_number()) {
            position_threshold_ = config["position_threshold"];
        }
        
        // Load warning threshold if available
        if (config.contains("warning_threshold") && config["warning_threshold"].is_number()) {
            warning_threshold_ = config["warning_threshold"];
        }
        
        // Load disable threshold if available
        if (config.contains("disable_threshold") && config["disable_threshold"].is_number()) {
            disable_threshold_ = config["disable_threshold"];
        }
        
        // Process device positions
        if (config.contains("devices") && config["devices"].is_array()) {
            for (const auto& device_obj : config["devices"]) {
                if (device_obj.contains("serial") && 
                    device_obj.contains("position") && 
                    device_obj.contains("rotation")) {
                    
                    std::string serial = device_obj["serial"];
                    auto it = device_map_.find(serial);
                    
                    if (it != device_map_.end()) {
                        size_t index = it->second;
                        
                        // Load device name if available
                        if (device_obj.contains("device_name") && device_obj["device_name"].is_string()) {
                            device_positions_[index].device_name = device_obj["device_name"];
                            // Also store in config
                            config_.device_names[serial] = device_obj["device_name"];
                        }
                        
                        // Load locked state
                        if (device_obj.contains("locked") && device_obj["locked"].is_boolean()) {
                            device_positions_[index].locked = device_obj["locked"];
                        }
                        
                        // Load include_in_locking state
                        if (device_obj.contains("include_in_locking") && device_obj["include_in_locking"].is_boolean()) {
                            device_positions_[index].include_in_locking = device_obj["include_in_locking"];
                        }
                        
                        // Load role if available
                        if (device_obj.contains("role") && device_obj["role"].is_number_integer()) {
                            int role_int = device_obj["role"];
                            device_positions_[index].role = static_cast<DeviceRole>(role_int);
                            // Also store in config
                            config_.device_roles[serial] = role_int;
                        }
                        
                        // Load position
                        auto position_array = device_obj["position"];
                        for (int i = 0; i < 3 && i < position_array.size(); i++) {
                            device_positions_[index].original_position[i] = position_array[i];
                        }
                        
                        // Load rotation
                        auto rotation_array = device_obj["rotation"];
                        for (int i = 0; i < 4 && i < rotation_array.size(); i++) {
                            device_positions_[index].original_rotation[i] = rotation_array[i];
                        }
                    } else {
                        // Device not currently connected, but store its name in config anyway
                        if (device_obj.contains("device_name") && device_obj["device_name"].is_string()) {
                            config_.device_names[serial] = device_obj["device_name"];
                        }
                    }
                }
            }
        }
        
        // Save config to ensure device names persist
        SaveConfig();
        
        if (StayPutVR::Logger::IsInitialized()) {
            StayPutVR::Logger::Info("UIManager: Loaded device positions from " + filepath);
        }
        
        current_config_file_ = filename;
        return true;
    }

    void UIManager::RenderMainWindow() {
        // Create main window that fills the entire viewport
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        
        // Set window flags to ensure scrolling is available if content doesn't fit
        ImGui::Begin("StayPutVR Control Panel", nullptr, 
            ImGuiWindowFlags_NoDecoration | 
            ImGuiWindowFlags_NoMove | 
            ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysVerticalScrollbar);
        
        // Title bar
        ImGui::Text("StayPutVR Control Panel");
        ImGui::Separator();
        
        // Render tab bar
        RenderTabBar();
        
        // Render content based on current tab
        switch (current_tab_) {
            case TabType::MAIN:
                RenderMainTab();
                break;
            case TabType::DEVICES:
                RenderDevicesTab();
                break;
            case TabType::BOUNDARIES:
                RenderBoundariesTab();
                break;
            case TabType::NOTIFICATIONS:
                RenderNotificationsTab();
                break;
            case TabType::TIMERS:
                RenderTimersTab();
                break;
            case TabType::OSC:
                RenderOSCTab();
                break;
            case TabType::PISHOCK:
                RenderPiShockTab();
                break;
            case TabType::OPENSHOCK:
                RenderOpenShockTab();
                break;
            case TabType::TWITCH:
                RenderTwitchTab();
                break;
            case TabType::SETTINGS:
                RenderSettingsTab();
                break;
        }
        
        ImGui::End();
    }
    
    void UIManager::RenderTabBar() {
        if (ImGui::BeginTabBar("##Tabs", ImGuiTabBarFlags_None)) {
            if (ImGui::BeginTabItem("Main")) {
                current_tab_ = TabType::MAIN;
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Devices")) {
                current_tab_ = TabType::DEVICES;
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Notifications")) {
                current_tab_ = TabType::NOTIFICATIONS;
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Timers")) {
                current_tab_ = TabType::TIMERS;
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("OSC")) {
                current_tab_ = TabType::OSC;
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("PiShock")) {
                current_tab_ = TabType::PISHOCK;
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("OpenShock")) {
                current_tab_ = TabType::OPENSHOCK;
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Twitch")) {
                current_tab_ = TabType::TWITCH;
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Settings")) {
                current_tab_ = TabType::SETTINGS;
                ImGui::EndTabItem();
            }
            
            ImGui::EndTabBar();
        }
    }
    
    void UIManager::RenderMainTab() {
        ImGui::Text("StayPutVR Main Control Panel");
        ImGui::Separator();
        
        // Connection status panel at the top
        ImGui::BeginChild("ConnectionPanel", ImVec2(0, 60), true);
        
        bool isConnected = device_manager_ && device_manager_->IsConnected();
        
        if (isConnected) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ Connected to driver");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "✗ Not connected to driver");
            ImGui::Text("The driver is not running or could not be reached");
            
            if (ImGui::Button("Retry Connection")) {
                if (device_manager_) {
                    // Use the new thread-safe manual reconnection method
                    if (device_manager_->ManualReconnect()) {
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Info("Successfully reconnected to driver");
                        }
                    } else {
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Warning("Failed to reconnect to driver");
                        }
                    }
                }
            }
        }
        
        ImGui::EndChild();
        
        // Emergency Stop Status Panel
        if (emergency_stop_active_) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.8f, 0.2f, 0.2f, 0.3f)); // Red background
            ImGui::BeginChild("EmergencyStopPanel", ImVec2(0, 80), true);
            
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f)); // Red text
            ImGui::Text("⚠️ EMERGENCY STOP MODE ACTIVE ⚠️");
            ImGui::PopStyleColor();
            
            ImGui::Text("All locking and disobedience actions are disabled.");
            ImGui::Text("All devices have been unlocked and cannot be re-locked.");
            
            ImGui::Spacing();
            
            if (ImGui::Button("Reset Emergency Stop", ImVec2(200, 30))) {
                ResetEmergencyStop();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Click to resume normal operation");
            
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
        
        // Layout with two columns
        ImGui::Columns(2, "MainTabColumns", false);
        
        // Status panel on the left column
        ImGui::BeginChild("StatusPanel", ImVec2(0, 200), true);
        ImGui::Text("Current Status:");
        ImGui::Separator();
        
        // Display active device count
        int active_devices = 0;
        int locked_devices = 0;
        int warning_devices = 0;
        int out_of_bounds_devices = 0;
        
        for (const auto& device : device_positions_) {
            active_devices++;
            if (device.locked || (device.include_in_locking && global_lock_active_)) {
                locked_devices++;
                
                // Calculate position deviation to check for warning/out of bounds
                float deviation = 0.0f;
                for (int i = 0; i < 3; i++) {
                    float diff = device.position[i] - device.original_position[i];
                    deviation += diff * diff;
                }
                deviation = std::sqrt(deviation);
                
                // Use the actual thresholds
                if (deviation > position_threshold_) {
                    out_of_bounds_devices++;
                } else if (deviation > warning_threshold_) {
                    warning_devices++;
                }
            }
        }
        
        ImGui::Text("Active Devices: %d", active_devices);
        ImGui::Text("Locked Devices: %d", locked_devices);
        
        if (global_lock_active_) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Global Lock Active");
        }
        
        // Count individually locked devices
        int individually_locked = 0;
        for (const auto& device : device_positions_) {
            if (device.locked) {
                individually_locked++;
            }
        }
        
        if (individually_locked > 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), 
                "%d device(s) individually locked", individually_locked);
        }
        
        // Show any warnings
        if (warning_devices > 0) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), 
                "%d device(s) in warning zone", warning_devices);
        }
        
        if (out_of_bounds_devices > 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 
                "%d device(s) out of bounds", out_of_bounds_devices);
        }
        
        if (locked_devices > 0 && warning_devices == 0 && out_of_bounds_devices == 0) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "All devices within safe zone");
        }
        
        ImGui::EndChild();
        
        ImGui::NextColumn();
        
        // Boundary Configuration (moved from Boundaries tab)
        ImGui::Text("Boundary Thresholds:");
        ImGui::Separator();
        
        bool changed = false;
        
        // Warning distance
        ImGui::Text("Safe Zone Radius:");
        if (ImGui::SliderFloat("##WarningDistance", &warning_threshold_, 0.01f, 0.5f, "%.2f m")) {
            config_.warning_threshold = warning_threshold_;
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Distance at which warning feedback begins");
            ImGui::EndTooltip();
        }
        
        // Out of bounds distance
        ImGui::Text("Warning Zone Radius:");
        if (ImGui::SliderFloat("##OutOfBoundsDistance", &position_threshold_, 0.05f, 1.0f, "%.2f m")) {
            config_.bounds_threshold = position_threshold_;
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Distance at which the device is considered out of bounds");
            ImGui::EndTooltip();
        }
        
        // Disable threshold
        ImGui::Text("Disable Distance:");
        if (ImGui::SliderFloat("##DisableThreshold", &disable_threshold_, 0.2f, 2.0f, "%.2f m")) {
            config_.disable_threshold = disable_threshold_;
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Distance at which locking is automatically disabled (safety feature)");
            ImGui::EndTooltip();
        }
        
        // Save changes if needed
        if (changed) {
            SaveConfig();
        }
        
        ImGui::Columns(1);
        
        // Device visualization with all boundary rings
        ImGui::BeginChild("DeviceVisualization", ImVec2(0, 300), true);
        ImGui::Text("Device Positions:");
        ImGui::Separator();
        
        // Create a visualization with all three rings
        const float canvas_size = 300.0f; // Increased size for better zoom
        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        ImVec2 canvas_center(canvas_pos.x + canvas_size/2, canvas_pos.y + canvas_size/2);
        
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        
        // Visualization scaling factor (increased for better zoom)
        // Adjust scale factor to make the rings match real-world distances better
        const float scale_factor = canvas_size/1.0f; // Changed from 1.5f to 1.0f for better scaling
        
        // Draw the boundary circles with consistent ratios
        float disable_radius = disable_threshold_ * scale_factor;
        float out_of_bounds_radius = position_threshold_ * scale_factor;
        float warning_radius = warning_threshold_ * scale_factor;
        
        draw_list->AddCircle(canvas_center, disable_radius, IM_COL32(255, 0, 0, 100), 0, 2.0f);
        draw_list->AddCircle(canvas_center, out_of_bounds_radius, IM_COL32(255, 128, 0, 150), 0, 2.0f);
        draw_list->AddCircle(canvas_center, warning_radius, IM_COL32(255, 255, 0, 150), 0, 2.0f);
        
        // Draw the safe zone
        draw_list->AddCircleFilled(canvas_center, warning_radius, IM_COL32(0, 255, 0, 50));
        
        // Draw each device that's included in locking
        for (const auto& device : device_positions_) {
            if (device.include_in_locking || device.locked) {
                // Calculate position deviation
                float deviation_x = 0.0f;
                float deviation_z = 0.0f;
                
                if (device.locked || (device.include_in_locking && global_lock_active_)) {
                    deviation_x = device.position[0] - device.original_position[0];
                    deviation_z = device.position[2] - device.original_position[2];
                }
                
                // Scale deviation to canvas (with better zoom)
                float scaled_x = deviation_x * scale_factor;
                float scaled_z = deviation_z * scale_factor;
                
                // Calculate device position on canvas
                ImVec2 device_pos(canvas_center.x + scaled_x, canvas_center.y + scaled_z);
                
                // Choose color based on device state and deviation
                ImU32 device_color;
                float total_deviation = std::sqrt(deviation_x*deviation_x + deviation_z*deviation_z);
                
                if (total_deviation > position_threshold_) {
                    device_color = IM_COL32(255, 0, 0, 255); // Red for out of bounds
                } else if (total_deviation > warning_threshold_) {
                    device_color = IM_COL32(255, 255, 0, 255); // Yellow for warning
                } else {
                    device_color = IM_COL32(0, 255, 0, 255); // Green for within bounds
                }
                
                // Draw the device as a small circle
                draw_list->AddCircleFilled(device_pos, 5.0f, device_color);
                
                // Use custom name if available, otherwise use device type
                std::string label;
                if (!device.device_name.empty()) {
                    label = device.device_name;
                } else {
                    // Default to device type
                    switch (device.type) {
                        case DeviceType::HMD: label = "HMD"; break;
                        case DeviceType::CONTROLLER: label = "CTRL"; break;
                        case DeviceType::TRACKER: label = "TRK"; break;
                        case DeviceType::TRACKING_REFERENCE: label = "BASE"; break;
                        default: label = "UNK"; break;
                    }
                }
                
                // Draw the label
                draw_list->AddText(ImVec2(device_pos.x + 7, device_pos.y - 7), device_color, label.c_str());
            }
        }
        
        // Add labels for the zones
        draw_list->AddText(ImVec2(canvas_center.x - 25, canvas_center.y - warning_radius - 15), 
                          IM_COL32(255, 255, 0, 255), "Warning Zone");
        draw_list->AddText(ImVec2(canvas_center.x - 35, canvas_center.y - out_of_bounds_radius - 15), 
                          IM_COL32(255, 128, 0, 255), "Out of Bounds Zone");
        draw_list->AddText(ImVec2(canvas_center.x - 15, canvas_center.y - disable_radius - 15), 
                          IM_COL32(255, 0, 0, 255), "Disable Zone");
        
        // Add the canvas space to ImGui
        ImGui::Dummy(ImVec2(canvas_size, canvas_size));
        
        ImGui::EndChild();
        
        // Add the lock button at the bottom of the Main tab
        ImGui::Separator();
        ImGui::Text("Global Lock Controls:");
        ImGui::TextWrapped("This affects all devices marked as 'Will Lock' in the Devices tab. Individual devices can be locked/unlocked in the Devices tab.");

        if (!global_lock_active_) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f)); // Green
            if (ImGui::Button("Lock All Included Devices", ImVec2(250, 40))) {
                ActivateGlobalLock(true);
            }
            ImGui::PopStyleColor();
            
            // Prompt to select devices if none are selected
            int devices_to_lock = 0;
            for (const auto& device : device_positions_) {
                if (device.include_in_locking) {
                    devices_to_lock++;
                }
            }
            
            if (devices_to_lock == 0) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Select devices to lock in the Devices tab first");
            } else {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.0f, 0.7f, 0.0f, 1.0f), "%d device(s) will be locked", devices_to_lock);
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f)); // Red
            if (ImGui::Button("Unlock All Included Devices", ImVec2(250, 40))) {
                ActivateGlobalLock(false);
            }
            ImGui::PopStyleColor();
            
            // Count locked devices
            int locked_devices = 0;
            for (const auto& device : device_positions_) {
                if (device.include_in_locking) {
                    locked_devices++;
                }
            }
            
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "%d device(s) will be unlocked", locked_devices);
        }
    }
    
    void UIManager::RenderDevicesTab() {
        ImGui::Text("Device Management");
        ImGui::Separator();
        RenderDeviceList();
    }
    
    void UIManager::RenderBoundariesTab() {
        // This tab is no longer used, as boundary settings were moved to the Main tab.
        ImGui::Text("Boundary settings have been moved to the Main tab.");
    }
    
    void UIManager::RenderNotificationsTab() {
        ImGui::Text("Notification Settings");
        ImGui::Separator();
        
        bool changed = false;
        
        // Audio notification settings
        ImGui::Text("Audio Notifications");
        ImGui::Separator();
        
        bool enable_audio = config_.audio_enabled;
        if (ImGui::Checkbox("Enable Audio Notifications", &enable_audio)) {
            config_.audio_enabled = enable_audio;
            changed = true;
        }
        
        // Audio volume
        float audio_volume = config_.audio_volume;
        if (ImGui::SliderFloat("Audio Volume", &audio_volume, 0.0f, 1.0f, "%.1f")) {
            config_.audio_volume = audio_volume;
            changed = true;
        }
        
        // Audio cue types
        bool warning_audio = config_.warning_audio;
        if (ImGui::Checkbox("Warning Sound", &warning_audio)) {
            config_.warning_audio = warning_audio;
            changed = true;
        }
        
        bool out_of_bounds_audio = config_.out_of_bounds_audio;
        if (ImGui::Checkbox("Out of Bounds Sound", &out_of_bounds_audio)) {
            config_.out_of_bounds_audio = out_of_bounds_audio;
            changed = true;
        }
        
        bool lock_audio = config_.lock_audio;
        if (ImGui::Checkbox("Lock Sound", &lock_audio)) {
            config_.lock_audio = lock_audio;
            changed = true;
        }
        
        bool unlock_audio = config_.unlock_audio;
        if (ImGui::Checkbox("Unlock Sound", &unlock_audio)) {
            config_.unlock_audio = unlock_audio;
            changed = true;
        }
        
        // Test buttons for sound effects
        ImGui::Separator();
        ImGui::Text("Test Audio:");
        
        // First row of buttons
        if (ImGui::Button("Test Warning Sound")) {
            AudioManager::PlayWarningSound(config_.audio_volume);
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Test Disobedience Sound")) {
            std::string filePath = StayPutVR::GetAppDataPath() + "\\resources\\disobedience.wav";
            if (std::filesystem::exists(filePath)) {
                AudioManager::PlaySound("disobedience.wav", config_.audio_volume);
            } else {
                if (StayPutVR::Logger::IsInitialized()) {
                    StayPutVR::Logger::Warning("disobedience.wav not found, please add it to the resources folder");
                }
                // Show a message in the UI that the file is missing
                ImGui::OpenPopup("Disobedience Sound Missing");
            }
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Test Success Sound")) {
            std::string filePath = StayPutVR::GetAppDataPath() + "\\resources\\success.wav";
            if (std::filesystem::exists(filePath)) {
                AudioManager::PlaySound("success.wav", config_.audio_volume);
            } else {
                if (StayPutVR::Logger::IsInitialized()) {
                    StayPutVR::Logger::Warning("success.wav not found, please add it to the resources folder");
                }
                // Show a message in the UI that the file is missing
                ImGui::OpenPopup("Success Sound Missing");
            }
        }
        
        // Second row of buttons
        if (ImGui::Button("Test Lock Sound")) {
            AudioManager::PlayLockSound(config_.audio_volume);
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Test Unlock Sound")) {
            AudioManager::PlayUnlockSound(config_.audio_volume);
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Test Countdown Sound")) {
            std::string filePath = StayPutVR::GetAppDataPath() + "\\resources\\countdown.wav";
            if (std::filesystem::exists(filePath)) {
                AudioManager::PlaySound("countdown.wav", config_.audio_volume);
            } else {
                if (StayPutVR::Logger::IsInitialized()) {
                    StayPutVR::Logger::Warning("countdown.wav not found, please add it to the resources folder");
                }
                // Show a message in the UI that the file is missing
                ImGui::OpenPopup("Countdown Sound Missing");
            }
        }
        
        // Add all popup blocks after the buttons
        if (ImGui::BeginPopup("Disobedience Sound Missing")) {
            ImGui::Text("disobedience.wav not found in resources folder");
            ImGui::Text("Please add the file to:");
            ImGui::Text("%s\\resources\\", StayPutVR::GetAppDataPath().c_str());
            ImGui::EndPopup();
        }
        
        if (ImGui::BeginPopup("Success Sound Missing")) {
            ImGui::Text("success.wav not found in resources folder");
            ImGui::Text("Please add the file to:");
            ImGui::Text("%s\\resources\\", StayPutVR::GetAppDataPath().c_str());
            ImGui::EndPopup();
        }
        
        if (ImGui::BeginPopup("Countdown Sound Missing")) {
            ImGui::Text("countdown.wav not found in resources folder");
            ImGui::Text("Please add the file to:");
            ImGui::Text("%s\\resources\\", StayPutVR::GetAppDataPath().c_str());
            ImGui::EndPopup();
        }
        
        // Save changes if anything was modified
        if (changed) {
            SaveConfig();
        }
    }
    
    void UIManager::RenderTimersTab() {
        ImGui::Text("Timer Settings");
        ImGui::Separator();
        
        bool changed = false;
        
        // Cooldown timeout
        ImGui::Text("Cooldown Timer");
        ImGui::Separator();
        
        bool cooldown_enabled = config_.cooldown_enabled;
        if (ImGui::Checkbox("Enable Cooldown After Unlock", &cooldown_enabled)) {
            config_.cooldown_enabled = cooldown_enabled;
            changed = true;
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("After unlocking, disables being locked again for this amount of time");
            ImGui::EndTooltip();
        }
        
        float cooldown_seconds = config_.cooldown_seconds;
        if (ImGui::SliderFloat("Cooldown Duration", &cooldown_seconds, 1.0f, 60.0f, "%.1f seconds")) {
            config_.cooldown_seconds = cooldown_seconds;
            changed = true;
        }
        
        // Countdown timer
        ImGui::Text("Countdown Timer");
        ImGui::Separator();
        
        bool countdown_enabled = config_.countdown_enabled;
        if (ImGui::Checkbox("Enable Countdown Before Lock", &countdown_enabled)) {
            config_.countdown_enabled = countdown_enabled;
            changed = true;
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("When enabled, a 3-second countdown sound will play before locking devices");
            ImGui::EndTooltip();
        }
        
        ImGui::Text("Countdown plays a 3-second countdown sound before locking devices.");
        
        ImGui::Spacing();
        ImGui::Separator();
        
        // Unlock Timer
        ImGui::Text("Unlock Timer");
        ImGui::Separator();
        
        bool unlock_timer_enabled = config_.unlock_timer_enabled;
        if (ImGui::Checkbox("Enable Automatic Unlock Timer", &unlock_timer_enabled)) {
            config_.unlock_timer_enabled = unlock_timer_enabled;
            changed = true;
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Automatically unlock devices after a specified duration (useful for Twitch integrations)");
            ImGui::EndTooltip();
        }
        
        ImGui::BeginDisabled(!config_.unlock_timer_enabled);
        
        float unlock_timer_minutes = config_.unlock_timer_duration / 60.0f;
        if (ImGui::SliderFloat("Unlock Timer Duration", &unlock_timer_minutes, 1.0f, 60.0f, "%.1f minutes")) {
            config_.unlock_timer_duration = unlock_timer_minutes * 60.0f;
            changed = true;
        }
        
        bool unlock_timer_show_remaining = config_.unlock_timer_show_remaining;
        if (ImGui::Checkbox("Show Remaining Time", &unlock_timer_show_remaining)) {
            config_.unlock_timer_show_remaining = unlock_timer_show_remaining;
            changed = true;
        }
        
        bool unlock_timer_audio_warnings = config_.unlock_timer_audio_warnings;
        if (ImGui::Checkbox("Audio Warnings", &unlock_timer_audio_warnings)) {
            config_.unlock_timer_audio_warnings = unlock_timer_audio_warnings;
            changed = true;
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Play audio warnings at 60s, 30s, and 10s remaining");
            ImGui::EndTooltip();
        }
        
        // Show current unlock timer status if active
        if (twitch_unlock_timer_active_) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Unlock Timer Active!");
            ImGui::Text("Time remaining: %.1f seconds", twitch_unlock_timer_remaining_);
            
            if (ImGui::Button("Cancel Unlock Timer")) {
                twitch_unlock_timer_active_ = false;
                twitch_unlock_timer_remaining_ = 0.0f;
            }
        }
        
        ImGui::EndDisabled();
        
        ImGui::Spacing();
        ImGui::Separator();
        
        ImGui::Text("Shock Cooldown Timer");
        ImGui::Separator();
        
        bool shock_cooldown_enabled = config_.shock_cooldown_enabled;
        if (ImGui::Checkbox("Enable Shock Cooldown", &shock_cooldown_enabled)) {
            config_.shock_cooldown_enabled = shock_cooldown_enabled;
            changed = true;
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Prevents shocks from being sent within the cooldown window for both PiShock and OpenShock");
            ImGui::EndTooltip();
        }
        
        ImGui::BeginDisabled(!config_.shock_cooldown_enabled);
        
        float shock_cooldown_seconds = config_.shock_cooldown_seconds;
        if (ImGui::SliderFloat("Shock Cooldown Duration", &shock_cooldown_seconds, 1.0f, 60.0f, "%.0f seconds")) {
            config_.shock_cooldown_seconds = shock_cooldown_seconds;
            changed = true;
        }
        
        ImGui::Text("Shocks will be blocked if sent within %.0f seconds of the last shock.", config_.shock_cooldown_seconds);
        
        ImGui::EndDisabled();
        
        // Save changes if anything was modified
        if (changed) {
            SaveConfig();
        }
    }
    
    void UIManager::RenderOSCTab() {
        ImGui::Text("OSC Configuration");
        ImGui::Separator();
        
        if (osc_enabled_) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("Disable OSC", ImVec2(150, 40))) {
                DisconnectOSC();
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "ACTIVE - Listening for OSC commands");
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
            if (ImGui::Button("Enable OSC", ImVec2(150, 40))) {
                HandleOSCConnection();
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "INACTIVE - Not listening for OSC commands");
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        
        // OSC connection settings
        ImGui::Text("OSC Connection");
        ImGui::Separator();
        
        // Create buffers for editing
        static char osc_ip[128];
        static int osc_send_port = 9000;
        static int osc_receive_port = 9001;
        
        // Initialize with current values
        if (strlen(osc_ip) == 0) {
            strcpy_s(osc_ip, sizeof(osc_ip), config_.osc_address.c_str());
        }
        if (osc_send_port != config_.osc_send_port) {
            osc_send_port = config_.osc_send_port;
        }
        if (osc_receive_port != config_.osc_receive_port) {
            osc_receive_port = config_.osc_receive_port;
        }
        
        // Inputs for OSC settings
        bool changed = false;
        
        if (ImGui::InputText("OSC IP Address", osc_ip, IM_ARRAYSIZE(osc_ip))) {
            config_.osc_address = osc_ip;
            changed = true;
        }
        
        if (ImGui::InputInt("OSC Send Port", &osc_send_port)) {
            config_.osc_send_port = osc_send_port;
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Port used to send locking status to VRChat");
            ImGui::EndTooltip();
        }
        
        if (ImGui::InputInt("OSC Receive Port", &osc_receive_port)) {
            config_.osc_receive_port = osc_receive_port;
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Port used to receive interaction data from VRChat, such as locking and unlocking");
            ImGui::EndTooltip();
        }
        
        // OSC Device Lock Paths
        ImGui::Separator();
        ImGui::Text("Device Lock Paths");
        ImGui::TextWrapped("These are the OSC addresses that the application will listen to for locking/unlocking signals. Sending a value of true/1 to these addresses will lock the corresponding device, false/0 will unlock it.");
        ImGui::Separator();
        
        // Device-specific paths
        
        // HMD
        static char hmd_path[128];
        if (strlen(hmd_path) == 0) {
            strcpy_s(hmd_path, sizeof(hmd_path), config_.osc_lock_path_hmd.c_str());
        }
        if (ImGui::InputText("HMD Lock Path", hmd_path, IM_ARRAYSIZE(hmd_path))) {
            config_.osc_lock_path_hmd = hmd_path;
            changed = true;
        }
        
        // Left Hand
        static char left_hand_path[128];
        if (strlen(left_hand_path) == 0) {
            strcpy_s(left_hand_path, sizeof(left_hand_path), config_.osc_lock_path_left_hand.c_str());
        }
        if (ImGui::InputText("Left Hand Lock Path", left_hand_path, IM_ARRAYSIZE(left_hand_path))) {
            config_.osc_lock_path_left_hand = left_hand_path;
            changed = true;
        }
        
        // Right Hand
        static char right_hand_path[128];
        if (strlen(right_hand_path) == 0) {
            strcpy_s(right_hand_path, sizeof(right_hand_path), config_.osc_lock_path_right_hand.c_str());
        }
        if (ImGui::InputText("Right Hand Lock Path", right_hand_path, IM_ARRAYSIZE(right_hand_path))) {
            config_.osc_lock_path_right_hand = right_hand_path;
            changed = true;
        }
        
        // Left Foot
        static char left_foot_path[128];
        if (strlen(left_foot_path) == 0) {
            strcpy_s(left_foot_path, sizeof(left_foot_path), config_.osc_lock_path_left_foot.c_str());
        }
        if (ImGui::InputText("Left Foot Lock Path", left_foot_path, IM_ARRAYSIZE(left_foot_path))) {
            config_.osc_lock_path_left_foot = left_foot_path;
            changed = true;
        }
        
        // Right Foot
        static char right_foot_path[128];
        if (strlen(right_foot_path) == 0) {
            strcpy_s(right_foot_path, sizeof(right_foot_path), config_.osc_lock_path_right_foot.c_str());
        }
        if (ImGui::InputText("Right Foot Lock Path", right_foot_path, IM_ARRAYSIZE(right_foot_path))) {
            config_.osc_lock_path_right_foot = right_foot_path;
            changed = true;
        }
        
        // Hip
        static char hip_path[128];
        if (strlen(hip_path) == 0) {
            strcpy_s(hip_path, sizeof(hip_path), config_.osc_lock_path_hip.c_str());
        }
        if (ImGui::InputText("Hip Lock Path", hip_path, IM_ARRAYSIZE(hip_path))) {
            config_.osc_lock_path_hip = hip_path;
            changed = true;
        }
        
        // Device Include Paths
        ImGui::Separator();
        ImGui::Text("Device Include Paths");
        ImGui::TextWrapped("These are the OSC addresses that toggle whether a device is included in locking. When a true/1 value is received, the include state for that device will be toggled.");
        ImGui::Separator();
        
        // HMD Include
        static char hmd_include_path[128];
        if (strlen(hmd_include_path) == 0) {
            strcpy_s(hmd_include_path, sizeof(hmd_include_path), config_.osc_include_path_hmd.c_str());
        }
        if (ImGui::InputText("HMD Include Path", hmd_include_path, IM_ARRAYSIZE(hmd_include_path))) {
            config_.osc_include_path_hmd = hmd_include_path;
            changed = true;
        }
        
        // Left Hand Include
        static char left_hand_include_path[128];
        if (strlen(left_hand_include_path) == 0) {
            strcpy_s(left_hand_include_path, sizeof(left_hand_include_path), config_.osc_include_path_left_hand.c_str());
        }
        if (ImGui::InputText("Left Hand Include Path", left_hand_include_path, IM_ARRAYSIZE(left_hand_include_path))) {
            config_.osc_include_path_left_hand = left_hand_include_path;
            changed = true;
        }
        
        // Right Hand Include
        static char right_hand_include_path[128];
        if (strlen(right_hand_include_path) == 0) {
            strcpy_s(right_hand_include_path, sizeof(right_hand_include_path), config_.osc_include_path_right_hand.c_str());
        }
        if (ImGui::InputText("Right Hand Include Path", right_hand_include_path, IM_ARRAYSIZE(right_hand_include_path))) {
            config_.osc_include_path_right_hand = right_hand_include_path;
            changed = true;
        }
        
        // Left Foot Include
        static char left_foot_include_path[128];
        if (strlen(left_foot_include_path) == 0) {
            strcpy_s(left_foot_include_path, sizeof(left_foot_include_path), config_.osc_include_path_left_foot.c_str());
        }
        if (ImGui::InputText("Left Foot Include Path", left_foot_include_path, IM_ARRAYSIZE(left_foot_include_path))) {
            config_.osc_include_path_left_foot = left_foot_include_path;
            changed = true;
        }
        
        // Right Foot Include
        static char right_foot_include_path[128];
        if (strlen(right_foot_include_path) == 0) {
            strcpy_s(right_foot_include_path, sizeof(right_foot_include_path), config_.osc_include_path_right_foot.c_str());
        }
        if (ImGui::InputText("Right Foot Include Path", right_foot_include_path, IM_ARRAYSIZE(right_foot_include_path))) {
            config_.osc_include_path_right_foot = right_foot_include_path;
            changed = true;
        }
        
        // Hip Include
        static char hip_include_path[128];
        if (strlen(hip_include_path) == 0) {
            strcpy_s(hip_include_path, sizeof(hip_include_path), config_.osc_include_path_hip.c_str());
        }
        if (ImGui::InputText("Hip Include Path", hip_include_path, IM_ARRAYSIZE(hip_include_path))) {
            config_.osc_include_path_hip = hip_include_path;
            changed = true;
        }
        
        // Global Lock/Unlock Paths
        ImGui::Separator();
        ImGui::Text("Global Lock/Unlock Paths");
        ImGui::TextWrapped("These are the OSC addresses that will trigger global lock or unlock of all enabled devices.");
        ImGui::Separator();
        
        // Global Lock
        static char global_lock_path[128];
        if (strlen(global_lock_path) == 0) {
            strcpy_s(global_lock_path, sizeof(global_lock_path), config_.osc_global_lock_path.c_str());
        }
        if (ImGui::InputText("Global Lock Path", global_lock_path, IM_ARRAYSIZE(global_lock_path))) {
            config_.osc_global_lock_path = global_lock_path;
            changed = true;
        }
        
        // Global Unlock
        static char global_unlock_path[128];
        if (strlen(global_unlock_path) == 0) {
            strcpy_s(global_unlock_path, sizeof(global_unlock_path), config_.osc_global_unlock_path.c_str());
        }
        if (ImGui::InputText("Global Unlock Path", global_unlock_path, IM_ARRAYSIZE(global_unlock_path))) {
            config_.osc_global_unlock_path = global_unlock_path;
            changed = true;
        }
        
        ImGui::Separator();
        ImGui::Text("Global Out-of-Bounds Trigger");
        ImGui::TextWrapped("Enable automatic out-of-bounds actions when receiving the SPVR_Global_OutOfBounds parameter from VRChat.");
        
        bool global_out_of_bounds_enabled = config_.osc_global_out_of_bounds_enabled;
        if (ImGui::Checkbox("Enable Global Out-of-Bounds Trigger", &global_out_of_bounds_enabled)) {
            config_.osc_global_out_of_bounds_enabled = global_out_of_bounds_enabled;
            changed = true;
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("When enabled, receiving the /avatar/parameters/SPVR_Global_OutOfBounds parameter will trigger out-of-bounds actions");
            ImGui::EndTooltip();
        }
        
        // Global out-of-bounds path input
        static char global_out_of_bounds_path[128];
        if (strlen(global_out_of_bounds_path) == 0) {
            strcpy_s(global_out_of_bounds_path, sizeof(global_out_of_bounds_path), config_.osc_global_out_of_bounds_path.c_str());
        }
        if (ImGui::InputText("Global Out-of-Bounds Path", global_out_of_bounds_path, IM_ARRAYSIZE(global_out_of_bounds_path))) {
            config_.osc_global_out_of_bounds_path = global_out_of_bounds_path;
            changed = true;
        }
        
        ImGui::Separator();
        ImGui::Text("Bite Trigger");
        ImGui::TextWrapped("Enable disobedience actions when receiving the SPVR_Bite parameter from VRChat.");
        
        bool bite_enabled = config_.osc_bite_enabled;
        if (ImGui::Checkbox("Enable Bite Trigger", &bite_enabled)) {
            config_.osc_bite_enabled = bite_enabled;
            changed = true;
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("When enabled, receiving the /avatar/parameters/SPVR_Bite parameter will trigger disobedience actions");
            ImGui::EndTooltip();
        }
        
        // Bite path input
        static char bite_path[128];
        if (strlen(bite_path) == 0) {
            strcpy_s(bite_path, sizeof(bite_path), config_.osc_bite_path.c_str());
        }
        if (ImGui::InputText("Bite Path", bite_path, IM_ARRAYSIZE(bite_path))) {
            config_.osc_bite_path = bite_path;
            changed = true;
        }
        
        ImGui::Separator();
        ImGui::Text("Emergency Stop Stretch");
        ImGui::TextWrapped("Enable emergency unlock when receiving the SPVR_EStop_Stretch parameter from VRChat with a value >= 0.5.");
        
        bool estop_stretch_enabled = config_.osc_estop_stretch_enabled;
        if (ImGui::Checkbox("Enable Emergency Stop Stretch", &estop_stretch_enabled)) {
            config_.osc_estop_stretch_enabled = estop_stretch_enabled;
            changed = true;
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("When enabled, receiving the /avatar/parameters/SPVR_EStop_Stretch parameter with value >= 0.5 will immediately unlock all devices (emergency stop)");
            ImGui::EndTooltip();
        }
        
        // Emergency stop stretch path input
        static char estop_stretch_path[128];
        if (strlen(estop_stretch_path) == 0) {
            strcpy_s(estop_stretch_path, sizeof(estop_stretch_path), config_.osc_estop_stretch_path.c_str());
        }
        if (ImGui::InputText("Emergency Stop Stretch Path", estop_stretch_path, IM_ARRAYSIZE(estop_stretch_path))) {
            config_.osc_estop_stretch_path = estop_stretch_path;
            changed = true;
        }
        
        // Chaining mode
        ImGui::Text("Chaining Mode");
        ImGui::Separator();
        
        bool chaining_mode = config_.chaining_mode;
        if (ImGui::Checkbox("Enable Chaining Mode", &chaining_mode)) {
            config_.chaining_mode = chaining_mode;
            changed = true;
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("When a device is locked via OSC, all devices will be locked");
            ImGui::EndTooltip();
        }
        
        // Reset to defaults button
        ImGui::Separator();
        if (ImGui::Button("Reset Paths to Defaults")) {
            // Reset device lock paths
            config_.osc_lock_path_hmd = "/avatar/parameters/SPVR_HMD_Latch_IsPosed";
            strcpy_s(hmd_path, sizeof(hmd_path), config_.osc_lock_path_hmd.c_str());
            
            config_.osc_lock_path_left_hand = "/avatar/parameters/SPVR_ControllerLeft_Latch_IsPosed";
            strcpy_s(left_hand_path, sizeof(left_hand_path), config_.osc_lock_path_left_hand.c_str());
            
            config_.osc_lock_path_right_hand = "/avatar/parameters/SPVR_ControllerRight_Latch_IsPosed";
            strcpy_s(right_hand_path, sizeof(right_hand_path), config_.osc_lock_path_right_hand.c_str());
            
            config_.osc_lock_path_left_foot = "/avatar/parameters/SPVR_FootLeft_Latch_IsPosed";
            strcpy_s(left_foot_path, sizeof(left_foot_path), config_.osc_lock_path_left_foot.c_str());
            
            config_.osc_lock_path_right_foot = "/avatar/parameters/SPVR_FootRight_Latch_IsPosed";
            strcpy_s(right_foot_path, sizeof(right_foot_path), config_.osc_lock_path_right_foot.c_str());
            
            config_.osc_lock_path_hip = "/avatar/parameters/SPVR_Hip_Latch_IsPosed";
            strcpy_s(hip_path, sizeof(hip_path), config_.osc_lock_path_hip.c_str());
            
            // Reset device include paths
            config_.osc_include_path_hmd = "/avatar/parameters/SPVR_HMD_include";
            strcpy_s(hmd_include_path, sizeof(hmd_include_path), config_.osc_include_path_hmd.c_str());
            
            config_.osc_include_path_left_hand = "/avatar/parameters/SPVR_ControllerLeft_include";
            strcpy_s(left_hand_include_path, sizeof(left_hand_include_path), config_.osc_include_path_left_hand.c_str());
            
            config_.osc_include_path_right_hand = "/avatar/parameters/SPVR_ControllerRight_include";
            strcpy_s(right_hand_include_path, sizeof(right_hand_include_path), config_.osc_include_path_right_hand.c_str());
            
            config_.osc_include_path_left_foot = "/avatar/parameters/SPVR_FootLeft_include";
            strcpy_s(left_foot_include_path, sizeof(left_foot_include_path), config_.osc_include_path_left_foot.c_str());
            
            config_.osc_include_path_right_foot = "/avatar/parameters/SPVR_FootRight_include";
            strcpy_s(right_foot_include_path, sizeof(right_foot_include_path), config_.osc_include_path_right_foot.c_str());
            
            config_.osc_include_path_hip = "/avatar/parameters/SPVR_Hip_include";
            strcpy_s(hip_include_path, sizeof(hip_include_path), config_.osc_include_path_hip.c_str());
            
            // Reset global lock/unlock paths
            config_.osc_global_lock_path = "/avatar/parameters/SPVR_Global_Lock";
            strcpy_s(global_lock_path, sizeof(global_lock_path), config_.osc_global_lock_path.c_str());
            
            config_.osc_global_unlock_path = "/avatar/parameters/SPVR_Global_Unlock";
            strcpy_s(global_unlock_path, sizeof(global_unlock_path), config_.osc_global_unlock_path.c_str());
            
            config_.osc_global_out_of_bounds_path = "/avatar/parameters/SPVR_Global_OutOfBounds";
            strcpy_s(global_out_of_bounds_path, sizeof(global_out_of_bounds_path), config_.osc_global_out_of_bounds_path.c_str());
            
            config_.osc_bite_path = "/avatar/parameters/SPVR_Bite";
            strcpy_s(bite_path, sizeof(bite_path), config_.osc_bite_path.c_str());
            
            // Update OSCManager with new paths
            if (osc_enabled_) {
                OSCManager::GetInstance().SetConfig(config_);
            }
            
            changed = true;
        }
        
        // Save changes
        if (changed) {
            SaveConfig();
            
            // If OSC is enabled, update the paths in the OSCManager
            if (osc_enabled_) {
                OSCManager::GetInstance().SetConfig(config_);
            }
        }
    }

    void UIManager::HandleOSCConnection() {
        if (osc_enabled_) {
            if (Logger::IsInitialized()) {
                Logger::Debug("HandleOSCConnection: OSC already enabled, verifying callbacks");
            }
            VerifyOSCCallbacks();
            return;
        }

        // Initialize OSC manager
        if (OSCManager::GetInstance().Initialize(config_.osc_address, config_.osc_send_port, config_.osc_receive_port)) {
            osc_enabled_ = true;
            config_.osc_enabled = true;
            SaveConfig();

            // Set the config for OSC paths
            OSCManager::GetInstance().SetConfig(config_);

            // Set up callbacks
            VerifyOSCCallbacks();

            if (Logger::IsInitialized()) {
                Logger::Info("OSC connection established");
            }
        } else {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to establish OSC connection");
            }
        }
    }

    void UIManager::DisconnectOSC() {
        if (!osc_enabled_) {
            return;
        }

        OSCManager::GetInstance().Shutdown();
        osc_enabled_ = false;
        config_.osc_enabled = false;
        SaveConfig();

        if (Logger::IsInitialized()) {
            Logger::Info("OSC connection closed");
        }
    }

    void UIManager::OnDeviceLocked(OSCDeviceType device, bool locked) {
        // Find devices with the appropriate role
        for (auto& dev : device_positions_) {
            // Check if this device has the role that matches the OSC device type
            DeviceRole targetRole = DeviceRole::None;
            
            switch (device) {
                case OSCDeviceType::HMD:
                    targetRole = DeviceRole::HMD;
                    break;
                case OSCDeviceType::ControllerLeft:
                    targetRole = DeviceRole::LeftController;
                    break;
                case OSCDeviceType::ControllerRight:
                    targetRole = DeviceRole::RightController;
                    break;
                case OSCDeviceType::FootLeft:
                    targetRole = DeviceRole::LeftFoot;
                    break;
                case OSCDeviceType::FootRight:
                    targetRole = DeviceRole::RightFoot;
                    break;
                case OSCDeviceType::Hip:
                    targetRole = DeviceRole::Hip;
                    break;
            }
            
            // Skip if no matching role
            if (dev.role != targetRole) {
                continue;
            }
            
            // Lock or unlock the device
            LockDevicePosition(dev.serial, locked);
            
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Info("Device " + dev.serial + " " + 
                    (locked ? "locked" : "unlocked") + " via OSC (role matching)");
            }
            
            // Play appropriate sound effect
            if (config_.audio_enabled) {
                if (locked && config_.lock_audio) {
                    AudioManager::PlayLockSound(config_.audio_volume);
                } else if (!locked && config_.unlock_audio) {
                    AudioManager::PlayUnlockSound(config_.audio_volume);
                }
            }
            
            // If chaining mode is enabled and we're locking, activate global lock
            if (locked && config_.chaining_mode) {
                ActivateGlobalLock(true);
            }
            
            // Update the device status via OSC
            UpdateDeviceStatus(device, locked ? DeviceStatus::LockedSafe : DeviceStatus::Unlocked);
        }
        
        // Diagnostic logging if no device was found with the matching role
        bool found_matching_device = false;
        for (auto& dev : device_positions_) {
            DeviceRole targetRole = DeviceRole::None;
            switch (device) {
                case OSCDeviceType::HMD: targetRole = DeviceRole::HMD; break;
                case OSCDeviceType::ControllerLeft: targetRole = DeviceRole::LeftController; break;
                case OSCDeviceType::ControllerRight: targetRole = DeviceRole::RightController; break;
                case OSCDeviceType::FootLeft: targetRole = DeviceRole::LeftFoot; break;
                case OSCDeviceType::FootRight: targetRole = DeviceRole::RightFoot; break;
                case OSCDeviceType::Hip: targetRole = DeviceRole::Hip; break;
            }
            if (dev.role == targetRole) {
                found_matching_device = true;
                break;
            }
        }
        
        if (!found_matching_device && StayPutVR::Logger::IsInitialized()) {
            StayPutVR::Logger::Warning("OSC received lock command for " + GetOSCDeviceString(device) + 
                                      " but no device with matching role was found. Command ignored.");
            
            // Print all current device roles for debugging
            std::string device_roles = "Current device roles: ";
            for (const auto& dev : device_positions_) {
                device_roles += dev.serial + "=";
                switch (dev.role) {
                    case DeviceRole::None: device_roles += "None"; break;
                    case DeviceRole::HMD: device_roles += "HMD"; break;
                    case DeviceRole::LeftController: device_roles += "LeftController"; break;
                    case DeviceRole::RightController: device_roles += "RightController"; break;
                    case DeviceRole::Hip: device_roles += "Hip"; break;
                    case DeviceRole::LeftFoot: device_roles += "LeftFoot"; break;
                    case DeviceRole::RightFoot: device_roles += "RightFoot"; break;
                    default: device_roles += "Unknown"; break;
                }
                device_roles += ", ";
            }
            if (!device_positions_.empty()) {
                device_roles.pop_back(); // Remove last comma
                device_roles.pop_back(); // Remove last space
            }
            StayPutVR::Logger::Debug(device_roles);
        }
    }

    void UIManager::UpdateDeviceStatus(OSCDeviceType device, DeviceStatus status) {
        if (!osc_enabled_) {
            return;
        }

        OSCManager::GetInstance().SendDeviceStatus(device, status);
    }
    
    // Helper function to map DeviceType to OSCDeviceType
    OSCDeviceType UIManager::MapToOSCDeviceType(DeviceType type) {
        switch (type) {
            case DeviceType::HMD: 
                return OSCDeviceType::HMD;
            case DeviceType::CONTROLLER: 
                // This is a simplification - in reality you'd need to determine which controller
                return OSCDeviceType::ControllerRight;
            default:
                return OSCDeviceType::HMD; // Default to HMD
        }
    }
    
    // Helper function to convert OSCDeviceType to string
    std::string UIManager::GetOSCDeviceString(OSCDeviceType device) const {
        switch (device) {
            case OSCDeviceType::HMD: return "HMD";
            case OSCDeviceType::ControllerLeft: return "ControllerLeft";
            case OSCDeviceType::ControllerRight: return "ControllerRight";
            case OSCDeviceType::FootLeft: return "FootLeft";
            case OSCDeviceType::FootRight: return "FootRight";
            case OSCDeviceType::Hip: return "Hip";
            default: return "Unknown";
        }
    }

    void UIManager::RenderSettingsTab() {
        ImGui::Text("Application Settings");
        ImGui::Separator();
        
        bool changed = false;
        
        // Configuration file management
        ImGui::Text("Configuration Management");
        ImGui::Separator();
        
        RenderConfigControls();
        
        // Logging configuration
        ImGui::Text("Logging Settings");
        ImGui::Separator();
        
        // Log level selection
        const char* log_levels[] = { "DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL" };
        static int current_log_level = 0;
        
        // Find the current log level in the array
        for (int i = 0; i < IM_ARRAYSIZE(log_levels); i++) {
            if (config_.log_level == log_levels[i]) {
                current_log_level = i;
                break;
            }
        }
        
        ImGui::Text("Log Level:");
        if (ImGui::Combo("##LogLevel", &current_log_level, log_levels, IM_ARRAYSIZE(log_levels))) {
            config_.log_level = log_levels[current_log_level];
            // Apply the new log level immediately
            StayPutVR::Logger::SetLogLevel(StayPutVR::Logger::StringToLogLevel(config_.log_level));
            Logger::Info("Log level changed to: " + config_.log_level);
            changed = true;
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::TextUnformatted("DEBUG: Most verbose, shows all log messages\n"
                                  "INFO: Shows informational messages and above\n"
                                  "WARNING: Shows warnings and errors only (default)\n"
                                  "ERROR: Shows only errors and critical messages\n"
                                  "CRITICAL: Shows only the most severe issues");
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        
        // Data folders
        ImGui::Text("Data Folders");
        ImGui::Separator();
        
        // Get data folders
        std::string appDataPath = GetAppDataPath();
        std::string logPath = appDataPath + "\\logs";
        std::string configPath = appDataPath + "\\config";
        std::string resourcesPath = appDataPath + "\\resources";
        
        // Display paths
        ImGui::Text("Settings Path: %s", configPath.c_str());
        ImGui::Text("Log Path: %s", logPath.c_str());
        ImGui::Text("Resources Path: %s", resourcesPath.c_str());
        
        // Add buttons to open folders in Explorer
        if (ImGui::Button("Open Settings Folder")) {
            ShellExecuteA(NULL, "open", configPath.c_str(), NULL, NULL, SW_SHOWDEFAULT);
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Open Log Folder")) {
            ShellExecuteA(NULL, "open", logPath.c_str(), NULL, NULL, SW_SHOWDEFAULT);
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Open Resources Folder")) {
            ShellExecuteA(NULL, "open", resourcesPath.c_str(), NULL, NULL, SW_SHOWDEFAULT);
        }
        
        // Audio files information
        ImGui::Separator();
        ImGui::Text("Audio Resources");
        ImGui::TextWrapped("To use custom sounds, place your audio files in the Resources folder:");
        ImGui::TextWrapped("- warning.wav - Played when device is in warning zone");
        ImGui::TextWrapped("- disobedience.wav - Played when device exceeds bounds");
        ImGui::TextWrapped("- success.wav - Played when device returns to safe zone");
        ImGui::TextWrapped("- lock.wav - Played when devices are locked");
        ImGui::TextWrapped("- unlock.wav - Played when devices are unlocked");
        ImGui::TextWrapped("- countdown.wav - Played during countdown timer");
        
        ImGui::Separator();
        
        // Application settings
        ImGui::Text("Application Settings");
        ImGui::Separator();
        
        // Force save all current configuration settings
        if (ImGui::Button("Save All Settings", ImVec2(150, 30))) {
            SaveConfig();
            ImGui::OpenPopup("SettingsSaved");
        }
        
        if (ImGui::BeginPopupModal("SettingsSaved", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("All settings have been saved!");
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        
        // About section
        ImGui::Text("About StayPutVR");
        ImGui::Separator();
        
        ImGui::Text("StayPutVR - Virtual Reality Position Locking");
        ImGui::Text("Version: 1.2.0");
        ImGui::Text("© 2025 Foxipso");
        ImGui::Text("foxipso.com");
        
        if (ImGui::Button("Visit Website")) {
            ShellExecuteA(NULL, "open", "https://foxipso.com", NULL, NULL, SW_SHOWDEFAULT);
        }
        
        // Save changes if anything was modified
        if (changed) {
            SaveConfig();
        }
    }

    void UIManager::RenderConfigControls() {
        // Check for existing config files
        std::vector<std::string> config_files;
        
        // Ensure the config directory exists
        std::filesystem::create_directories(config_dir_);
        
        for (const auto& entry : std::filesystem::directory_iterator(config_dir_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                config_files.push_back(entry.path().stem().string());
            }
        }
        
        ImGui::PushID("ConfigSection");
        
        ImGui::Text("Configuration");
        ImGui::Separator();
        
        // Load configuration
        if (!config_files.empty()) {
            static int selected_config = -1;
            if (selected_config >= (int)config_files.size()) {
                selected_config = -1;
            }
            
            ImGui::Text("Load Configuration:");
            
            if (ImGui::BeginListBox("##ConfigList")) {
                for (int i = 0; i < config_files.size(); i++) {
                    const bool is_selected = (selected_config == i);
                    if (ImGui::Selectable(config_files[i].c_str(), is_selected)) {
                        selected_config = i;
                    }
                    
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndListBox();
            }
            
            if (selected_config >= 0) {
                if (ImGui::Button("Load Selected")) {
                    if (LoadDevicePositions(config_files[selected_config])) {
                        ImGui::OpenPopup("LoadSuccess");
                    } else {
                        ImGui::OpenPopup("LoadFailed");
                    }
                }
                
                ImGui::SameLine();
                if (ImGui::Button("Delete Selected")) {
                    ImGui::OpenPopup("ConfirmDelete");
                }
                
                // Confirm delete popup
                if (ImGui::BeginPopupModal("ConfirmDelete", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Are you sure you want to delete this configuration?");
                    ImGui::Text("%s", config_files[selected_config].c_str());
                    ImGui::Separator();
                    
                    if (ImGui::Button("Yes", ImVec2(120, 0))) {
                        std::string filepath = config_dir_ + "\\" + config_files[selected_config] + ".json";
                        try {
                            std::filesystem::remove(filepath);
                            selected_config = -1;
                        } catch (const std::exception& e) {
                            // Handle error
                            if (StayPutVR::Logger::IsInitialized()) {
                                StayPutVR::Logger::Error("UIManager: Failed to delete config file: " + std::string(e.what()));
                            }
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("No", ImVec2(120, 0))) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
            }
        } else {
            ImGui::Text("No saved configurations found.");
        }
        
        // Reset all devices
        if (ImGui::Button("Reset All Devices")) {
            ResetAllDevices();
        }
        
        // Success/Failure popups
        if (ImGui::BeginPopupModal("SaveSuccess", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Configuration saved successfully!");
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        
        if (ImGui::BeginPopupModal("SaveFailed", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Failed to save configuration!");
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        
        if (ImGui::BeginPopupModal("LoadSuccess", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Configuration loaded successfully!");
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        
        if (ImGui::BeginPopupModal("LoadFailed", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Failed to load configuration!");
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        
        ImGui::PopID();
        ImGui::Separator();
    }

    void UIManager::RenderLockControls() {
        ImGui::PushID("LockSection");
        
        ImGui::Text("Position Lock Controls");
        ImGui::Separator();
        
        // Position threshold slider
        ImGui::Text("Position Threshold:");
        ImGui::SliderFloat("##PosThreshold", &position_threshold_, 0.01f, 0.5f, "%.2f m");
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::TextUnformatted("Maximum distance a device can move from its locked position before being flagged.");
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        
        // Global lock button
        if (global_lock_active_) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("Unlock All Devices")) {
                ActivateGlobalLock(false);
            }
            ImGui::PopStyleColor();
            
            // Display status if any device exceeds threshold
            bool any_exceeds = false;
            for (const auto& device : device_positions_) {
                if (device.include_in_locking && device.exceeds_threshold) {
                    any_exceeds = true;
                    break;
                }
            }
            
            if (any_exceeds) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "WARNING: Some devices have moved beyond threshold!");
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
            if (ImGui::Button("Lock Selected Devices")) {
                ActivateGlobalLock(true);
            }
            ImGui::PopStyleColor();
        }
        
        ImGui::PopID();
        ImGui::Separator();
    }

    void UIManager::RenderDeviceList() {
        ImGui::Text("Connected Devices: %zu", device_positions_.size());
        ImGui::Separator();
        
        if (ImGui::BeginTable("DevicesTable", 6, ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("Device Info");
            ImGui::TableSetupColumn("Role");
            ImGui::TableSetupColumn("Position & Rotation");
            ImGui::TableSetupColumn("Status");
            ImGui::TableSetupColumn("Lock Controls");
            ImGui::TableSetupColumn("Shock Devices");
            ImGui::TableHeadersRow();
            
            for (auto& device : device_positions_) {
                ImGui::TableNextRow();
                
                // Device Info (Type + Serial combined)
                ImGui::TableNextColumn();
                const char* type_str = "Unknown";
                switch (device.type) {
                    case DeviceType::HMD: type_str = "HMD"; break;
                    case DeviceType::CONTROLLER: type_str = "Controller"; break;
                    case DeviceType::TRACKER: type_str = "Tracker"; break;
                    case DeviceType::TRACKING_REFERENCE: type_str = "Base Station"; break;
                }
                ImGui::Text("%s", type_str);
                ImGui::Text("Serial: %s", device.serial.c_str());
                
                // Device Role Dropdown
                ImGui::TableNextColumn();
                ImGui::PushID(("deviceRole" + device.serial).c_str());
                
                // Current role as string for display
                std::string current_role_str = OSCManager::GetInstance().GetRoleString(device.role);
                
                if (ImGui::BeginCombo("##DeviceRole", current_role_str.c_str())) {
                    // None option
                    bool is_selected = (device.role == DeviceRole::None);
                    if (ImGui::Selectable("None", is_selected)) {
                        device.role = DeviceRole::None;
                        // Save the role in config
                        config_.device_roles[device.serial] = static_cast<int>(device.role);
                        SaveConfig();
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Info("Device role changed: " + device.serial + " -> None");
                        }
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                    
                    // HMD option
                    is_selected = (device.role == DeviceRole::HMD);
                    if (ImGui::Selectable("HMD", is_selected)) {
                        device.role = DeviceRole::HMD;
                        config_.device_roles[device.serial] = static_cast<int>(device.role);
                        SaveConfig();
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Info("Device role changed: " + device.serial + " -> HMD");
                        }
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                    
                    // Left Controller option
                    is_selected = (device.role == DeviceRole::LeftController);
                    if (ImGui::Selectable("Left Controller", is_selected)) {
                        device.role = DeviceRole::LeftController;
                        config_.device_roles[device.serial] = static_cast<int>(device.role);
                        SaveConfig();
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Info("Device role changed: " + device.serial + " -> Left Controller");
                        }
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                    
                    // Right Controller option
                    is_selected = (device.role == DeviceRole::RightController);
                    if (ImGui::Selectable("Right Controller", is_selected)) {
                        device.role = DeviceRole::RightController;
                        config_.device_roles[device.serial] = static_cast<int>(device.role);
                        SaveConfig();
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Info("Device role changed: " + device.serial + " -> Right Controller");
                        }
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                    
                    // Hip option
                    is_selected = (device.role == DeviceRole::Hip);
                    if (ImGui::Selectable("Hip", is_selected)) {
                        device.role = DeviceRole::Hip;
                        config_.device_roles[device.serial] = static_cast<int>(device.role);
                        SaveConfig();
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Info("Device role changed: " + device.serial + " -> Hip");
                        }
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                    
                    // Left Foot option
                    is_selected = (device.role == DeviceRole::LeftFoot);
                    if (ImGui::Selectable("Left Foot", is_selected)) {
                        device.role = DeviceRole::LeftFoot;
                        config_.device_roles[device.serial] = static_cast<int>(device.role);
                        SaveConfig();
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Info("Device role changed: " + device.serial + " -> Left Foot");
                        }
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                    
                    // Right Foot option
                    is_selected = (device.role == DeviceRole::RightFoot);
                    if (ImGui::Selectable("Right Foot", is_selected)) {
                        device.role = DeviceRole::RightFoot;
                        config_.device_roles[device.serial] = static_cast<int>(device.role);
                        SaveConfig();
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Info("Device role changed: " + device.serial + " -> Right Foot");
                        }
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                    
                    ImGui::EndCombo();
                }
                
                ImGui::PopID();
                
                // Position & Rotation
                ImGui::TableNextColumn();
                
                // Check if position has been updated recently (within last 500ms)
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - device.last_update_time).count();
                bool recently_updated = elapsed < 500;
                
                // Show position with color indicator if recently updated
                if (recently_updated && !device.locked) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f)); // Green text for active updates
                } else if (device.locked) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.0f, 1.0f)); // Orange text for locked devices
                } else if (device.include_in_locking && global_lock_active_) {
                    if (device.exceeds_threshold) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f)); // Red text for devices exceeding threshold
                    } else {
                        // Calculate position deviation to check for warning
                        float deviation = 0.0f;
                        for (int i = 0; i < 3; i++) {
                            float diff = device.position[i] - device.original_position[i];
                            deviation += diff * diff;
                        }
                        deviation = std::sqrt(deviation);
                        
                        // Use the actual warning threshold
                        if (deviation > warning_threshold_) {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f)); // Yellow text for warning
                        } else {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.7f, 1.0f, 1.0f)); // Blue text for globally locked devices
                        }
                    }
                }
                
                ImGui::Text("Pos: (%.2f, %.2f, %.2f)", 
                    device.position[0], device.position[1], device.position[2]);
                ImGui::Text("Rot: (%.2f, %.2f, %.2f, %.2f)", 
                    device.rotation[0], device.rotation[1], 
                    device.rotation[2], device.rotation[3]);
                
                if (recently_updated || device.locked || (device.include_in_locking && global_lock_active_)) {
                    ImGui::PopStyleColor();
                }
                
                // Status column
                ImGui::TableNextColumn();
                
                // Show movement indicator
                float movement = 0.0f;
                for (int i = 0; i < 3; i++) {
                    movement += std::abs(device.position[i] - device.previous_position[i]);
                }
                
                if (movement > 0.001f && !device.locked && !(device.include_in_locking && global_lock_active_)) {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "[MOVING]");
                }
                
                if (device.include_in_locking && global_lock_active_) {
                    // Calculate position deviation to check for warning/out of bounds
                    float deviation = 0.0f;
                    for (int i = 0; i < 3; i++) {
                        float diff = device.position[i] - device.original_position[i];
                        deviation += diff * diff;
                    }
                    deviation = std::sqrt(deviation);
                    
                    if (device.exceeds_threshold) {
                        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 
                            "[OUT OF BOUNDS: %.2f m]", device.position_deviation);
                    } else if (deviation > warning_threshold_) {
                        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), 
                            "[WARNING: %.2f m]", deviation);
                    } else {
                        ImGui::TextColored(ImVec4(0.0f, 0.7f, 1.0f, 1.0f), 
                            "[LOCKED: %.2f m]", device.position_deviation);
                    }
                } else if (device.locked) {
                    // Show deviation status for individually locked devices
                    if (device.exceeds_threshold) {
                        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 
                            "[OUT OF BOUNDS: %.2f m]", device.position_deviation);
                    } else if (device.in_warning_zone) {
                        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), 
                            "[WARNING: %.2f m]", device.position_deviation);
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), 
                            "[INDIVIDUALLY LOCKED: %.2f m]", device.position_deviation);
                    }
                }
                
                // Lock Controls column (combined include and lock/unlock)
                ImGui::TableNextColumn();
                ImGui::PushID(("lockControls" + device.serial).c_str());
                
                // Include in locking toggle button
                if (device.include_in_locking) {
                    // Green "Will Lock" button
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.9f, 0.3f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.7f, 0.1f, 1.0f));
                    
                    if (ImGui::Button("Will Lock", ImVec2(80, 25))) {
                        device.include_in_locking = false;
                        
                        // Update the setting in config directly
                        config_.device_settings[device.serial] = false;
                        
                        // Save the setting immediately
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Info("Device " + device.serial + " include_in_locking set to false");
                        }
                        SaveConfig();
                    }
                    
                    ImGui::PopStyleColor(3);
                } else {
                    // Red "Won't Lock" button
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
                    
                    if (ImGui::Button("Won't Lock", ImVec2(80, 25))) {
                        device.include_in_locking = true;
                        
                        // Update the setting in config directly
                        config_.device_settings[device.serial] = true;
                        
                        // Save the setting immediately
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Info("Device " + device.serial + " include_in_locking set to true");
                        }
                        SaveConfig();
                    }
                    
                    ImGui::PopStyleColor(3);
                }
                
                // Individual lock/unlock button on the same line
                ImGui::SameLine();
                
                if (device.locked) {
                    // Orange "Unlock" button
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.6f, 0.1f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.4f, 0.0f, 1.0f));
                    
                    if (ImGui::Button("Unlock", ImVec2(60, 25))) {
                        // Individual device unlocking
                        LockDevicePosition(device.serial, false);
                        
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Info("Device " + device.serial + " individually unlocked");
                        }
                        
                        // Play unlock sound if enabled
                        if (config_.audio_enabled && config_.unlock_audio) {
                            AudioManager::PlayUnlockSound(config_.audio_volume);
                        }
                    }
                    
                    ImGui::PopStyleColor(3);
                } else {
                    // Blue "Lock" button
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.5f, 1.0f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.1f, 0.6f, 1.0f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.4f, 0.9f, 1.0f));
                    
                    if (ImGui::Button("Lock", ImVec2(60, 25))) {
                        // Individual device locking
                        LockDevicePosition(device.serial, true);
                        
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Info("Device " + device.serial + " individually locked");
                        }
                        
                        // Play lock sound if enabled
                        if (config_.audio_enabled && config_.lock_audio) {
                            AudioManager::PlayLockSound(config_.audio_volume);
                        }
                    }
                    
                    ImGui::PopStyleColor(3);
                }
                
                ImGui::PopID();
                
                // Shock Devices column - NEW ADDITION
                ImGui::TableNextColumn();
                ImGui::PushID(("shockDevices" + device.serial).c_str());
                
                // Load device shock settings from config
                auto shock_it = config_.device_shock_ids.find(device.serial);
                if (shock_it != config_.device_shock_ids.end()) {
                    device.shock_device_enabled = shock_it->second;
                }
                
                // Small buttons for shock device selection (1-5)
                ImGui::Text("Shock IDs:");
                for (int i = 0; i < 5; ++i) {
                    // Show button if either OpenShock or PiShock device is configured
                    bool has_device = !config_.openshock_device_ids[i].empty() || config_.pishock_shocker_ids[i] != 0;
                    if (has_device) {
                        ImGui::PushID(i);
                        
                        // Color the button based on whether it's enabled
                        if (device.shock_device_enabled[i]) {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.9f, 0.3f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.7f, 0.1f, 1.0f));
                        } else {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
                        }
                        
                        if (ImGui::Button(std::to_string(i + 1).c_str(), ImVec2(25, 25))) {
                            device.shock_device_enabled[i] = !device.shock_device_enabled[i];
                            config_.device_shock_ids[device.serial] = device.shock_device_enabled;
                            SaveConfig();
                        }
                        
                        ImGui::PopStyleColor(3);
                        ImGui::PopID();
                        
                        if (i < 4) ImGui::SameLine();
                    }
                }
                
                // All/None buttons
                ImGui::NewLine();
                if (ImGui::Button("All", ImVec2(40, 20))) {
                    for (int i = 0; i < 5; ++i) {
                        bool has_device = !config_.openshock_device_ids[i].empty() || config_.pishock_shocker_ids[i] != 0;
                        if (has_device) {
                            device.shock_device_enabled[i] = true;
                        }
                    }
                    config_.device_shock_ids[device.serial] = device.shock_device_enabled;
                    SaveConfig();
                }
                ImGui::SameLine();
                if (ImGui::Button("None", ImVec2(40, 20))) {
                    for (int i = 0; i < 5; ++i) {
                        device.shock_device_enabled[i] = false;
                    }
                    config_.device_shock_ids[device.serial] = device.shock_device_enabled;
                    SaveConfig();
                }
                
                ImGui::PopID();
            }
            
            ImGui::EndTable();
        }
    }

    void UIManager::GlfwErrorCallback(int error, const char* description) {
        std::cerr << "GLFW Error " << error << ": " << description << std::endl;
    }

    bool UIManager::LoadConfig() {
        try {
            // Use AppData path for the config file
            std::string configDir = GetAppDataPath() + "\\config";
            std::filesystem::create_directories(configDir);
            
            // Store just the filename in the member variable
            config_file_ = "config.ini";
            
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Info("UIManager: Loading config from " + configDir + "\\" + config_file_);
            }
            
            // Pass just the filename to LoadFromFile
            bool result = config_.LoadFromFile(config_file_);
            if (result) {
                UpdateUIFromConfig();
                
                // Set default OSC ports if they're not set
                if (config_.osc_send_port <= 0) {
                    config_.osc_send_port = 9000;
                }
                if (config_.osc_receive_port <= 0) {
                    config_.osc_receive_port = 9001;
                }
                
                if (StayPutVR::Logger::IsInitialized()) {
                    StayPutVR::Logger::Info("UIManager: Config loaded successfully");
                }
            } else {
                // Set default OSC ports for new config
                config_.osc_send_port = 9000;
                config_.osc_receive_port = 9001;
                
                if (StayPutVR::Logger::IsInitialized()) {
                    StayPutVR::Logger::Error("UIManager: Failed to load config");
                }
            }
            return result;
        }
        catch (const std::exception& e) {
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Error("UIManager: Exception in LoadConfig: " + std::string(e.what()));
            }
            return false;
        }
    }

    bool UIManager::SaveConfig() {
        try {
            UpdateConfigFromUI();
            
            // Use AppData path for reference only
            std::string configDir = GetAppDataPath() + "\\config";
            std::filesystem::create_directories(configDir);
            
            // config_file_ should already be set to just the filename
            
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Info("UIManager: Saving config to " + configDir + "\\" + config_file_);
            }
            
            // Pass just the filename to SaveToFile
            bool result = config_.SaveToFile(config_file_);
            if (!result && StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Error("UIManager: Failed to save config");
            }
            return result;
        }
        catch (const std::exception& e) {
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Error("UIManager: Exception in SaveConfig: " + std::string(e.what()));
            }
            return false;
        }
    }

    void UIManager::UpdateConfigFromUI() {
        // Boundary settings
        config_.warning_threshold = warning_threshold_;
        config_.bounds_threshold = position_threshold_;
        config_.disable_threshold = disable_threshold_;
        
        // Store device data from currently connected devices
        // DO NOT clear the maps - this would erase settings for disconnected devices
        
        for (const auto& device : device_positions_) {
            // Store device name if not empty
            if (!device.device_name.empty()) {
                config_.device_names[device.serial] = device.device_name;
            }
            
            // Store include_in_locking setting
            config_.device_settings[device.serial] = device.include_in_locking;
            
            // Store device role
            config_.device_roles[device.serial] = static_cast<int>(device.role);
            
            if (StayPutVR::Logger::IsInitialized() && device.role != DeviceRole::None) {
                StayPutVR::Logger::Debug("Saved role for device: " + device.serial + 
                    " -> role value: " + std::to_string(static_cast<int>(device.role)) + 
                    " (" + OSCManager::GetInstance().GetRoleString(device.role) + ")");
            }
        }
        
        if (StayPutVR::Logger::IsInitialized()) {
            StayPutVR::Logger::Info("Updated configuration from UI for " + 
                                    std::to_string(device_positions_.size()) + " devices");
        }
    }

    void UIManager::UpdateUIFromConfig() {
        if (StayPutVR::Logger::IsInitialized()) {
            Logger::Info("UpdateUIFromConfig: Beginning to update UI from loaded config");
        }
        
        // Boundary settings
        warning_threshold_ = config_.warning_threshold;
        position_threshold_ = config_.bounds_threshold;
        disable_threshold_ = config_.disable_threshold;
        
        // Update OSC status
        osc_enabled_ = config_.osc_enabled;
        
        if (StayPutVR::Logger::IsInitialized()) {
            if (!config_.device_roles.empty()) {
                Logger::Info("Found " + std::to_string(config_.device_roles.size()) + " device roles in config");
            } else {
                Logger::Warning("No device roles found in config");
            }
        }
        
        // Apply device names and settings
        for (auto& device : device_positions_) {
            // Apply device name if available
            auto nameIt = config_.device_names.find(device.serial);
            if (nameIt != config_.device_names.end()) {
                device.device_name = nameIt->second;
                if (StayPutVR::Logger::IsInitialized()) {
                    Logger::Info("Applied name for device: " + device.serial + " -> " + nameIt->second);
                }
            }
            
            // Apply include_in_locking setting if available
            auto settingIt = config_.device_settings.find(device.serial);
            if (settingIt != config_.device_settings.end()) {
                device.include_in_locking = settingIt->second;
                if (StayPutVR::Logger::IsInitialized()) {
                    Logger::Info("Applied include_in_locking setting for device: " + device.serial + 
                                " -> " + (settingIt->second ? "true" : "false"));
                }
            }
            
            // Apply device role if available
            auto roleIt = config_.device_roles.find(device.serial);
            if (roleIt != config_.device_roles.end()) {
                int roleValue = roleIt->second;
                device.role = static_cast<DeviceRole>(roleValue);
                
                if (StayPutVR::Logger::IsInitialized()) {
                    std::string roleName = "Unknown";
                    switch (device.role) {
                        case DeviceRole::None: roleName = "None"; break;
                        case DeviceRole::HMD: roleName = "HMD"; break;
                        case DeviceRole::LeftController: roleName = "LeftController"; break;
                        case DeviceRole::RightController: roleName = "RightController"; break;
                        case DeviceRole::Hip: roleName = "Hip"; break;
                        case DeviceRole::LeftFoot: roleName = "LeftFoot"; break;
                        case DeviceRole::RightFoot: roleName = "RightFoot"; break;
                        default: roleName = "Unknown"; break;
                    }
                    Logger::Info("Applied role for device: " + device.serial + 
                                " -> role value: " + std::to_string(roleValue) + 
                                " (Role: " + roleName + ")");
                }
            } else {
                if (StayPutVR::Logger::IsInitialized()) {
                    Logger::Warning("No role found in config for device: " + device.serial);
                }
            }
        }
        
        if (StayPutVR::Logger::IsInitialized()) {
            Logger::Info("UpdateUIFromConfig: Finished updating UI from config");
        }
    }

    void UIManager::RenderPiShockTab() {
        ImGui::Text("PiShock Integration");
        ImGui::Separator();
        
        // Safety warning (at the top)
        ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - 20);
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "WARNING: Safety Information");
        ImGui::Text("PiShock should only be used in accordance with their safety instructions. The makers of StayPutVR accept and assume no liability for your usage of PiShock, even if you use it in a manner you deem to be safe. This is for entertainment purposes only. When in doubt, use a low intensity and double-check all safety information, including safe placement of the device. The makers are not liable for any and all coding defects that may cause this feature to operate improperly. There is no express or implied guarantee that this feature will work properly.");
        ImGui::PopTextWrapPos();
        
        // Add agreement checkbox
        bool user_agreement = config_.pishock_user_agreement;
        if (ImGui::Checkbox("I understand and agree to the safety information above", &user_agreement)) {
            config_.pishock_user_agreement = user_agreement;
            SaveConfig();
        }
        
        ImGui::Separator();
        
        // ===== COMMON SETTINGS (both modes) =====
        ImGui::BeginDisabled(!user_agreement);
        
        // Enable checkbox
        bool pishock_enabled = config_.pishock_enabled;
        if (ImGui::Checkbox("Enable PiShock Integration", &pishock_enabled)) {
            config_.pishock_enabled = pishock_enabled;
            SaveConfig();
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Enable direct integration with PiShock API for out-of-bounds enforcement");
            ImGui::EndTooltip();
        }
        
        ImGui::Spacing();
        
        // Common username field
        ImGui::Text("Username:");
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Username you use to log into PiShock.com");
            ImGui::EndTooltip();
        }
        
        static char username_buffer[128] = "";
        if (strlen(username_buffer) == 0 && !config_.pishock_username.empty()) {
            strcpy_s(username_buffer, sizeof(username_buffer), config_.pishock_username.c_str());
        }
        
        if (ImGui::InputText("##Username", username_buffer, sizeof(username_buffer))) {
            config_.pishock_username = username_buffer;
            SaveConfig();
        }
        
        ImGui::Separator();
        
        // ===== MODE SELECTION =====
        ImGui::Text("API Mode:");
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Legacy API: HTTP-based API (deprecated)\nWebSocket v2: Real-time persistent connection with lower latency and continuous shocking (recommended)");
            ImGui::EndTooltip();
        }
        
        const char* modes[] = { "Legacy HTTP API", "WebSocket v2" };
        int current_mode = static_cast<int>(config_.pishock_mode);
        if (ImGui::Combo("##PiShockMode", &current_mode, modes, 2)) {
            auto old_mode = config_.pishock_mode;
            config_.pishock_mode = static_cast<Config::PiShockMode>(current_mode);
            SaveConfig();
            
            // Handle mode switching - disconnect old, prepare new
            if (old_mode == Config::PiShockMode::WEBSOCKET_V2 && pishock_ws_manager_) {
                pishock_ws_manager_->Disconnect();
            }
            
            Logger::Info("PiShock mode changed to: " + std::string(modes[current_mode]));
        }
        
        ImGui::Separator();
        
        // ===== MODE-SPECIFIC SUBTABS =====
        if (ImGui::BeginTabBar("##PiShockSubTabs", ImGuiTabBarFlags_None)) {
            
            // Configuration Tab
            if (ImGui::BeginTabItem("Configuration")) {
                ImGui::Spacing();
                
                if (config_.pishock_mode == Config::PiShockMode::LEGACY_API) {
                    // ===== LEGACY API CONFIGURATION =====
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Legacy HTTP API Configuration");
                    ImGui::Separator();
                    
                    ImGui::Text("API Key:");
                    ImGui::SameLine();
                    ImGui::TextDisabled("(?)");
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted("API Key generated on PiShock.com (found in Account section)");
                        ImGui::EndTooltip();
                    }
                    
                    static char apikey_buffer[128] = "";
                    if (strlen(apikey_buffer) == 0 && !config_.pishock_api_key.empty()) {
                        strcpy_s(apikey_buffer, sizeof(apikey_buffer), config_.pishock_api_key.c_str());
                    }
                    
                    if (ImGui::InputText("##APIKey", apikey_buffer, sizeof(apikey_buffer), ImGuiInputTextFlags_Password)) {
                        config_.pishock_api_key = apikey_buffer;
                        SaveConfig();
                    }
                    
                    ImGui::Text("Share Code:");
                    ImGui::SameLine();
                    ImGui::TextDisabled("(?)");
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted("Share Code generated on PiShock.com for the device you want to control");
                        ImGui::EndTooltip();
                    }
                    
                    static char sharecode_buffer[128] = "";
                    if (strlen(sharecode_buffer) == 0 && !config_.pishock_share_code.empty()) {
                        strcpy_s(sharecode_buffer, sizeof(sharecode_buffer), config_.pishock_share_code.c_str());
                    }
                    
                    if (ImGui::InputText("##ShareCode", sharecode_buffer, sizeof(sharecode_buffer), ImGuiInputTextFlags_Password)) {
                        config_.pishock_share_code = sharecode_buffer;
                        SaveConfig();
                    }
                    
                    // Status
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Text("Status:");
                    if (pishock_manager_) {
                        ImGui::SameLine();
                        std::string status = pishock_manager_->GetConnectionStatus();
                        if (status == "Ready") {
                            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s", status.c_str());
                        } else {
                            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", status.c_str());
                        }
                    }
                    
                } else {
                    // ===== WEBSOCKET V2 CONFIGURATION =====
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "WebSocket v2 Configuration");
                    ImGui::Separator();
                    
                    ImGui::Text("API Key:");
                    ImGui::SameLine();
                    ImGui::TextDisabled("(?)");
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted("API Key generated on PiShock.com (Account section)\nRequired for WebSocket authentication");
                        ImGui::EndTooltip();
                    }
                    
                    static char ws_apikey_buffer[128] = "";
                    if (strlen(ws_apikey_buffer) == 0 && !config_.pishock_api_key.empty()) {
                        strcpy_s(ws_apikey_buffer, sizeof(ws_apikey_buffer), config_.pishock_api_key.c_str());
                    }
                    
                    if (ImGui::InputText("##WSAPIKey", ws_apikey_buffer, sizeof(ws_apikey_buffer), ImGuiInputTextFlags_Password)) {
                        config_.pishock_api_key = ws_apikey_buffer;
                        SaveConfig();
                    }
                    
                    ImGui::Text("Client ID:");
                    ImGui::SameLine();
                    ImGui::TextDisabled("(?)");
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted("Your PiShock Client ID (hub ID)\nUsed to identify the device that your shockers connect to");
                        ImGui::EndTooltip();
                    }
                    
                    static char ws_clientid_buffer[128] = "";
                    if (strlen(ws_clientid_buffer) == 0 && !config_.pishock_client_id.empty()) {
                        strcpy_s(ws_clientid_buffer, sizeof(ws_clientid_buffer), config_.pishock_client_id.c_str());
                    }
                    
                    if (ImGui::InputText("##WSClientID", ws_clientid_buffer, sizeof(ws_clientid_buffer))) {
                        config_.pishock_client_id = ws_clientid_buffer;
                        SaveConfig();
                    }
                    
                    ImGui::Text("Shocker IDs:");
                    ImGui::SameLine();
                    ImGui::TextDisabled("(?)");
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted("PiShock shocker device IDs. Device 0 is considered the master device.\nLeave unused slots at 0.");
                        ImGui::EndTooltip();
                    }
                    
                    static int shocker_id_buffers[5] = {0, 0, 0, 0, 0};
                    
                    for (int i = 0; i < 5; ++i) {
                        // Sync buffers with config values
                        if (config_.pishock_shocker_ids[i] != shocker_id_buffers[i]) {
                            shocker_id_buffers[i] = config_.pishock_shocker_ids[i];
                        }
                        
                        std::string label = std::to_string(i) + ": ";
                        if (i == 0) {
                            label += "(master)";
                        }
                        
                        ImGui::PushID(i);
                        if (ImGui::InputInt(label.c_str(), &shocker_id_buffers[i])) {
                            config_.pishock_shocker_ids[i] = shocker_id_buffers[i];
                            SaveConfig();
                        }
                        ImGui::PopID();
                    }
                    
                    ImGui::Spacing();
                    ImGui::Separator();
                    
                    // Connection controls
                    ImGui::Text("Connection:");
                    
                    bool is_connected = pishock_ws_manager_ && pishock_ws_manager_->IsConnected();
                    
                    if (is_connected) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Connected");
                        
                        ImGui::SameLine();
                        if (ImGui::Button("Disconnect##WSDisconnect")) {
                            if (pishock_ws_manager_) {
                                pishock_ws_manager_->Disconnect();
                            }
                        }
                    } else {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Disconnected");
                        
                        ImGui::SameLine();
                        // Check if at least one shocker ID is configured
                        bool has_shocker_id = false;
                        for (const auto& id : config_.pishock_shocker_ids) {
                            if (id != 0) {
                                has_shocker_id = true;
                                break;
                            }
                        }
                        
                        bool can_connect = !config_.pishock_username.empty() && 
                                         !config_.pishock_api_key.empty() &&
                                         !config_.pishock_client_id.empty() &&
                                         has_shocker_id;
                        
                        ImGui::BeginDisabled(!can_connect);
                        if (ImGui::Button("Connect##WSConnect")) {
                            if (pishock_ws_manager_) {
                                if (!pishock_ws_manager_->Connect()) {
                                    Logger::Error("Failed to connect to PiShock WebSocket: " + 
                                                pishock_ws_manager_->GetLastError());
                                }
                            }
                        }
                        ImGui::EndDisabled();
                    }
                    
                    // Status
                    ImGui::Spacing();
                    ImGui::Text("Status:");
                    if (pishock_ws_manager_) {
                        ImGui::SameLine();
                        std::string status = pishock_ws_manager_->GetConnectionStatus();
                        if (status == "Connected") {
                            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s", status.c_str());
                        } else if (status == "Disconnected") {
                            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", status.c_str());
                        } else {
                            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", status.c_str());
                        }
                        
                        std::string last_error = pishock_ws_manager_->GetLastError();
                        if (!last_error.empty()) {
                            ImGui::Text("Last Error:");
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", last_error.c_str());
                        }
                    }
                }
                
                ImGui::EndTabItem();
            }
            
            // Actions Tab
            if (ImGui::BeginTabItem("Actions")) {
                ImGui::Spacing();
                
                ImGui::TextWrapped("PiShock will only be triggered when a device exceeds the out-of-bounds threshold. Warnings will only use audio cues.");
                ImGui::Spacing();
                ImGui::Separator();
                
                ImGui::BeginDisabled(!config_.pishock_enabled);
                
                // Out of Bounds Actions
                ImGui::Text("Out of Bounds Actions:");
                
                bool disobedience_beep = config_.pishock_disobedience_beep;
                if (ImGui::Checkbox("Beep on Out of Bounds", &disobedience_beep)) {
                    config_.pishock_disobedience_beep = disobedience_beep;
                    SaveConfig();
                }
                
                bool disobedience_vibrate = config_.pishock_disobedience_vibrate;
                if (ImGui::Checkbox("Vibrate on Out of Bounds", &disobedience_vibrate)) {
                    config_.pishock_disobedience_vibrate = disobedience_vibrate;
                    SaveConfig();
                }
                
                bool disobedience_shock = config_.pishock_disobedience_shock;
                if (ImGui::Checkbox("Shock on Out of Bounds", &disobedience_shock)) {
                    config_.pishock_disobedience_shock = disobedience_shock;
                    SaveConfig();
                }
                
                ImGui::Spacing();
                
                // Individual device disobedience intensity settings
                bool use_individual_disobedience = config_.pishock_use_individual_disobedience_intensities;
                if (ImGui::Checkbox("Use Individual Device Disobedience Intensities", &use_individual_disobedience)) {
                    config_.pishock_use_individual_disobedience_intensities = use_individual_disobedience;
                    SaveConfig();
                }
                
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted("Enable to set different disobedience intensity levels for each PiShock device. When disabled, all devices use the master disobedience intensity.");
                    ImGui::EndTooltip();
                }
                
                if (!use_individual_disobedience) {
                    // Master disobedience intensity slider
                    float disobedience_intensity = config_.pishock_disobedience_intensity;
                    if (ImGui::SliderFloat("Intensity", &disobedience_intensity, 0.0f, 1.0f, "%.2f")) {
                        config_.pishock_disobedience_intensity = disobedience_intensity;
                        SaveConfig();
                    }
                } else {
                    // Individual device disobedience intensity sliders
                    ImGui::Text("Individual Device Disobedience Intensities:");
                    ImGui::Indent();
                    
                    for (int i = 0; i < 5; ++i) {
                        if (config_.pishock_shocker_ids[i] != 0) {
                            ImGui::PushID(i + 200); // Different ID range to avoid conflicts
                            
                            std::string disobedience_label = "Device " + std::to_string(i) + " Intensity";
                            float individual_disobedience_intensity = config_.pishock_individual_disobedience_intensities[i];
                            
                            if (ImGui::SliderFloat(disobedience_label.c_str(), &individual_disobedience_intensity, 0.0f, 1.0f, "%.2f")) {
                                config_.pishock_individual_disobedience_intensities[i] = individual_disobedience_intensity;
                                SaveConfig();
                            }
                            
                            ImGui::PopID();
                        }
                    }
                    
                    ImGui::Unindent();
                }
                
                float disobedience_duration = config_.pishock_disobedience_duration;
                if (ImGui::SliderFloat("Duration", &disobedience_duration, 1.0f, 15.0f, "%.2f seconds")) {
                    disobedience_duration = (std::max)(1.0f, (std::min)(15.0f, disobedience_duration));
                    config_.pishock_disobedience_duration = disobedience_duration;
                    SaveConfig();
                }
                
                ImGui::Spacing();
                ImGui::Separator();
                
                // Test buttons
                ImGui::Text("Test:");
                
                bool can_test = config_.pishock_enabled;
                
                // Mode-specific validation
                if (config_.pishock_mode == Config::PiShockMode::LEGACY_API) {
                    // Legacy API needs username, API key, and share code
                    can_test = can_test && 
                              !config_.pishock_username.empty() && 
                              !config_.pishock_api_key.empty() && 
                              !config_.pishock_share_code.empty();
                } else if (config_.pishock_mode == Config::PiShockMode::WEBSOCKET_V2) {
                    // WebSocket v2 needs to be connected
                    can_test = can_test && pishock_ws_manager_ && pishock_ws_manager_->IsConnected();
                }
                
                ImGui::BeginDisabled(!can_test);
                
                if (ImGui::Button("Test Out of Bounds Actions")) {
                    if (config_.pishock_mode == Config::PiShockMode::LEGACY_API && pishock_manager_) {
                        pishock_manager_->TestActions();
                    } else if (config_.pishock_mode == Config::PiShockMode::WEBSOCKET_V2 && pishock_ws_manager_) {
                        pishock_ws_manager_->TestActions();
                    }
                }
                
                ImGui::EndDisabled();
                
                if (!can_test && config_.pishock_mode == Config::PiShockMode::WEBSOCKET_V2) {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Connect to WebSocket first");
                }
                
                ImGui::EndDisabled(); // End pishock_enabled disabled
                
                ImGui::EndTabItem();
            }
            
            ImGui::EndTabBar();
        }
        
        ImGui::EndDisabled(); // End of the user_agreement disabled block
    }

    // PiShock Helper Methods Implementation
    void UIManager::InitializePiShockManager() {
        pishock_manager_ = std::make_unique<PiShockManager>();
        
        if (pishock_manager_->Initialize(&config_)) {
            // Set up callback for PiShock action results
            pishock_manager_->SetActionCallback(
                [this](const std::string& action_type, bool success, const std::string& message) {
                    if (Logger::IsInitialized()) {
                        Logger::Info("PiShock " + action_type + " " + (success ? "succeeded" : "failed") + 
                                   (message.empty() ? "" : ": " + message));
                    }
                }
            );
            
            if (Logger::IsInitialized()) {
                Logger::Info("PiShockManager initialized successfully");
            }
        } else {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to initialize PiShockManager");
            }
        }
    }

    void UIManager::InitializePiShockWebSocketManager() {
        pishock_ws_manager_ = std::make_unique<PiShockWebSocketManager>();
        
        if (pishock_ws_manager_->Initialize(&config_)) {
            // Set up callback for PiShock WebSocket action results
            pishock_ws_manager_->SetActionCallback(
                [this](const std::string& action_type, bool success, const std::string& message) {
                    if (Logger::IsInitialized()) {
                        Logger::Info("PiShock WebSocket " + action_type + " " + (success ? "succeeded" : "failed") + 
                                   (message.empty() ? "" : ": " + message));
                    }
                }
            );
            
            if (Logger::IsInitialized()) {
                Logger::Info("PiShockWebSocketManager initialized successfully");
            }
            
            // Auto-connect if WebSocket v2 is selected and fully configured
            if (config_.pishock_mode == Config::PiShockMode::WEBSOCKET_V2 && 
                config_.pishock_enabled &&
                pishock_ws_manager_->IsFullyConfigured()) {
                Logger::Info("Auto-connecting to PiShock WebSocket v2...");
                if (pishock_ws_manager_->Connect()) {
                    Logger::Info("Auto-connected to PiShock WebSocket v2");
                } else {
                    Logger::Warning("Failed to auto-connect to PiShock WebSocket v2: " + 
                                  pishock_ws_manager_->GetLastError());
                }
            }
        } else {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to initialize PiShockWebSocketManager");
            }
        }
    }

    void UIManager::ShutdownPiShockManager() {
        if (pishock_manager_) {
            pishock_manager_->Shutdown();
            pishock_manager_.reset();
            
            if (Logger::IsInitialized()) {
                Logger::Info("PiShockManager shut down");
            }
        }
        
        if (pishock_ws_manager_) {
            pishock_ws_manager_->Shutdown();
            pishock_ws_manager_.reset();
            
            if (Logger::IsInitialized()) {
                Logger::Info("PiShockWebSocketManager shut down");
            }
        }
    }

    void UIManager::TriggerPiShockDisobedience(const std::string& device_serial) {
        if (config_.pishock_mode == Config::PiShockMode::LEGACY_API) {
            if (pishock_manager_ && pishock_manager_->IsEnabled()) {
                pishock_manager_->TriggerDisobedienceActions(device_serial);
            }
        } else if (config_.pishock_mode == Config::PiShockMode::WEBSOCKET_V2) {
            if (pishock_ws_manager_ && pishock_ws_manager_->IsEnabled()) {
                pishock_ws_manager_->TriggerDisobedienceActions(device_serial);
            }
        }
    }

    void UIManager::TriggerPiShockWarning(const std::string& device_serial) {
        if (config_.pishock_mode == Config::PiShockMode::LEGACY_API) {
            if (pishock_manager_ && pishock_manager_->IsEnabled()) {
                pishock_manager_->TriggerWarningActions(device_serial);
            }
        } else if (config_.pishock_mode == Config::PiShockMode::WEBSOCKET_V2) {
            if (pishock_ws_manager_ && pishock_ws_manager_->IsEnabled()) {
                pishock_ws_manager_->TriggerWarningActions(device_serial);
            }
        }
    }

    bool UIManager::CanTriggerPiShock() const {
        if (config_.pishock_mode == Config::PiShockMode::LEGACY_API) {
            return pishock_manager_ && pishock_manager_->IsEnabled() && pishock_manager_->CanTriggerAction();
        } else if (config_.pishock_mode == Config::PiShockMode::WEBSOCKET_V2) {
            return pishock_ws_manager_ && pishock_ws_manager_->IsEnabled() && pishock_ws_manager_->CanTriggerAction();
        }
        return false;
    }

    void UIManager::VerifyOSCCallbacks() {
        if (!osc_enabled_) {
            if (Logger::IsInitialized()) {
                Logger::Warning("VerifyOSCCallbacks: OSC is not enabled");
            }
            return;
        }
        
        // Re-register callbacks to ensure they're properly set
        OSCManager::GetInstance().SetLockCallback(
            [this](OSCDeviceType device, bool locked) {
                OnDeviceLocked(device, locked);
            }
        );
        
        OSCManager::GetInstance().SetIncludeCallback(
            [this](OSCDeviceType device, bool include) {
                OnDeviceIncluded(device, include);
            }
        );
        
        OSCManager::GetInstance().SetGlobalLockCallback(
            [this](bool lock) {
                if (Logger::IsInitialized()) {
                    Logger::Info("Global " + std::string(lock ? "lock" : "unlock") + " triggered via OSC");
                }
                // Use the same method the UI uses, which handles countdown and sound effects
                ActivateGlobalLock(lock);
            }
        );
        
        OSCManager::GetInstance().SetGlobalOutOfBoundsCallback(
            [this](bool triggered) {
                if (!config_.osc_global_out_of_bounds_enabled) {
                    return; // Feature disabled
                }
                if (Logger::IsInitialized()) {
                    Logger::Info("Global out-of-bounds triggered via OSC");
                }
                TriggerGlobalOutOfBoundsActions();
            }
        );
        
        OSCManager::GetInstance().SetBiteCallback(
            [this](bool triggered) {
                if (!config_.osc_bite_enabled) {
                    return;
                }
                if (Logger::IsInitialized()) {
                    Logger::Info("Bite triggered via OSC");
                }
                TriggerBiteActions();
            }
        );
        
        OSCManager::GetInstance().SetEStopStretchCallback(
            [this](float stretch_value) {
                if (!config_.osc_estop_stretch_enabled) {
                    return;
                }
                if (Logger::IsInitialized()) {
                    Logger::Info("Emergency stop stretch triggered via OSC with value: " + std::to_string(stretch_value) + " - entering emergency stop mode");
                }
                
                // Enter emergency stop mode
                emergency_stop_active_ = true;
                
                // Unlock all devices immediately
                ActivateGlobalLock(false);
                
                // Also unlock any individually locked devices
                for (auto& device : device_positions_) {
                    if (device.locked) {
                        LockDevicePosition(device.serial, false);
                    }
                }
                
                if (Logger::IsInitialized()) {
                    Logger::Warning("EMERGENCY STOP MODE ACTIVE - All actions disabled until reset");
                }
            }
        );
        
        if (Logger::IsInitialized()) {
            Logger::Info("VerifyOSCCallbacks: OSC callbacks verified and re-registered");
        }
    }

    void UIManager::OnDeviceIncluded(OSCDeviceType device, bool include) {
        // Find devices with the appropriate role
        for (auto& dev : device_positions_) {
            // Check if this device has the role that matches the OSC device type
            DeviceRole targetRole = DeviceRole::None;
            
            switch (device) {
                case OSCDeviceType::HMD:
                    targetRole = DeviceRole::HMD;
                    break;
                case OSCDeviceType::ControllerLeft:
                    targetRole = DeviceRole::LeftController;
                    break;
                case OSCDeviceType::ControllerRight:
                    targetRole = DeviceRole::RightController;
                    break;
                case OSCDeviceType::FootLeft:
                    targetRole = DeviceRole::LeftFoot;
                    break;
                case OSCDeviceType::FootRight:
                    targetRole = DeviceRole::RightFoot;
                    break;
                case OSCDeviceType::Hip:
                    targetRole = DeviceRole::Hip;
                    break;
            }
            
            // Skip if no matching role
            if (dev.role != targetRole) {
                continue;
            }
            
            // Toggle the include_in_locking flag rather than setting it directly
            dev.include_in_locking = !dev.include_in_locking;
            
            // Save to config
            config_.device_settings[dev.serial] = dev.include_in_locking;
            SaveConfig();
            
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Info("Device " + dev.serial + " include_in_locking toggled to " + 
                                       (dev.include_in_locking ? "true" : "false") + " via OSC");
            }
        }
        
        bool found_matching_device = false;
        for (auto& dev : device_positions_) {
            DeviceRole targetRole = DeviceRole::None;
            switch (device) {
                case OSCDeviceType::HMD: targetRole = DeviceRole::HMD; break;
                case OSCDeviceType::ControllerLeft: targetRole = DeviceRole::LeftController; break;
                case OSCDeviceType::ControllerRight: targetRole = DeviceRole::RightController; break;
                case OSCDeviceType::FootLeft: targetRole = DeviceRole::LeftFoot; break;
                case OSCDeviceType::FootRight: targetRole = DeviceRole::RightFoot; break;
                case OSCDeviceType::Hip: targetRole = DeviceRole::Hip; break;
            }
            if (dev.role == targetRole) {
                found_matching_device = true;
                break;
            }
        }
        
        if (!found_matching_device && StayPutVR::Logger::IsInitialized()) {
            StayPutVR::Logger::Warning("OSC received include command for " + GetOSCDeviceString(device) + 
                                      " but no device with matching role was found. Command ignored.");
            
            // Print all current device roles for debugging
            std::string device_roles = "Current device roles: ";
            for (const auto& dev : device_positions_) {
                device_roles += dev.serial + "=";
                switch (dev.role) {
                    case DeviceRole::None: device_roles += "None"; break;
                    case DeviceRole::HMD: device_roles += "HMD"; break;
                    case DeviceRole::LeftController: device_roles += "LeftController"; break;
                    case DeviceRole::RightController: device_roles += "RightController"; break;
                    case DeviceRole::Hip: device_roles += "Hip"; break;
                    case DeviceRole::LeftFoot: device_roles += "LeftFoot"; break;
                    case DeviceRole::RightFoot: device_roles += "RightFoot"; break;
                    default: device_roles += "Unknown"; break;
                }
                device_roles += ", ";
            }
            if (!device_positions_.empty()) {
                device_roles.pop_back(); // Remove last comma
                device_roles.pop_back(); // Remove last space
            }
            StayPutVR::Logger::Debug(device_roles);
        }
    }

    // Helper function to convert DeviceRole to OSCDeviceType
    OSCDeviceType UIManager::DeviceRoleToOSCDeviceType(DeviceRole role) const {
        switch (role) {
            case DeviceRole::HMD: return OSCDeviceType::HMD;
            case DeviceRole::LeftController: return OSCDeviceType::ControllerLeft;
            case DeviceRole::RightController: return OSCDeviceType::ControllerRight;
            case DeviceRole::LeftFoot: return OSCDeviceType::FootLeft;
            case DeviceRole::RightFoot: return OSCDeviceType::FootRight;
            case DeviceRole::Hip: return OSCDeviceType::Hip;
            default: return OSCDeviceType::HMD; // Default fallback, though this shouldn't be used for None role
        }
    }

    void UIManager::RenderTwitchTab() {
        ImGui::Text("Twitch Integration");
        ImGui::Separator();
        
        // Safety warning (similar to PiShock)
        ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - 20);
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "WARNING: Safety Information");
        ImGui::Text("Twitch integration allows viewers to trigger device locking through donations/bits/subscriptions. This should only be used with people you trust and in safe environments. The makers of StayPutVR accept no liability for misuse of this feature. Always have a safety mechanism to quickly disconnect devices if needed. Test all features thoroughly before use with live viewers.");
        ImGui::PopTextWrapPos();
        
        // User agreement checkbox
        bool user_agreement = config_.twitch_user_agreement;
        if (ImGui::Checkbox("I understand and agree to the safety information above", &user_agreement)) {
            config_.twitch_user_agreement = user_agreement;
            SaveConfig();
        }
        
        ImGui::Separator();
        
        // Main enable/disable checkbox (disabled until agreement is checked)
        ImGui::BeginDisabled(!user_agreement);
        bool twitch_enabled = config_.twitch_enabled;
        if (ImGui::Checkbox("Enable Twitch Integration", &twitch_enabled)) {
            config_.twitch_enabled = twitch_enabled;
            SaveConfig();
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Enable Twitch API integration for chat bot and donation triggers");
            ImGui::EndTooltip();
        }
        
        // Connection status
        if (config_.twitch_enabled && twitch_manager_) {
            std::string status = twitch_manager_->GetConnectionStatus();
            if (twitch_manager_->IsConnected()) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ("✓ " + status).c_str());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), ("✗ " + status).c_str());
                std::string error = twitch_manager_->GetLastError();
                if (!error.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), ("Error: " + error).c_str());
                }
            }
        }
        
        ImGui::Separator();
        
        // Twitch API Authentication
        ImGui::Text("Twitch API Authentication:");
        ImGui::TextWrapped("You'll need to create a Twitch application at https://dev.twitch.tv/console to get these credentials.");
        
        static char client_id_buffer[128] = "";
        // Always sync buffer with current config value
        if (config_.twitch_client_id != client_id_buffer) {
            strcpy_s(client_id_buffer, sizeof(client_id_buffer), config_.twitch_client_id.c_str());
        }
        
        if (ImGui::InputText("Client ID", client_id_buffer, sizeof(client_id_buffer))) {
            config_.twitch_client_id = client_id_buffer;
            SaveConfig();
        }
        
        static char client_secret_buffer[128] = "";
        // Always sync buffer with current config value
        if (config_.twitch_client_secret != client_secret_buffer) {
            strcpy_s(client_secret_buffer, sizeof(client_secret_buffer), config_.twitch_client_secret.c_str());
        }
        
        if (ImGui::InputText("Client Secret", client_secret_buffer, sizeof(client_secret_buffer), ImGuiInputTextFlags_Password)) {
            config_.twitch_client_secret = client_secret_buffer;
            SaveConfig();
        }
        
        static char channel_name_buffer[128] = "";
        // Always sync buffer with current config value
        if (config_.twitch_channel_name != channel_name_buffer) {
            strcpy_s(channel_name_buffer, sizeof(channel_name_buffer), config_.twitch_channel_name.c_str());
        }
        
        if (ImGui::InputText("Channel Name", channel_name_buffer, sizeof(channel_name_buffer))) {
            config_.twitch_channel_name = channel_name_buffer;
            SaveConfig();
        }
        
        static char bot_username_buffer[128] = "";
        // Always sync buffer with current config value
        if (config_.twitch_bot_username != bot_username_buffer) {
            strcpy_s(bot_username_buffer, sizeof(bot_username_buffer), config_.twitch_bot_username.c_str());
        }
        
        if (ImGui::InputText("Bot Username", bot_username_buffer, sizeof(bot_username_buffer))) {
            config_.twitch_bot_username = bot_username_buffer;
            SaveConfig();
        }
        
        // OAuth buttons
        ImGui::Spacing();
        ImGui::BeginDisabled(!config_.twitch_enabled || config_.twitch_client_id.empty() || config_.twitch_client_secret.empty());
        
        if (ImGui::Button("Connect to Twitch")) {
            if (twitch_manager_) {
                twitch_manager_->ConnectToTwitch();
            }
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Disconnect from Twitch")) {
            if (twitch_manager_) {
                twitch_manager_->DisconnectFromTwitch();
            }
        }
        
        ImGui::EndDisabled();
        
        ImGui::Separator();
        
        // Chat Bot Settings
        ImGui::Text("Chat Bot Settings:");
        
        bool chat_enabled = config_.twitch_chat_enabled;
        if (ImGui::Checkbox("Enable Chat Commands", &chat_enabled)) {
            config_.twitch_chat_enabled = chat_enabled;
            SaveConfig();
        }
        
        ImGui::BeginDisabled(!config_.twitch_chat_enabled);
        
        static char prefix_buffer[16] = "";
        // Always sync buffer with current config value
        if (config_.twitch_command_prefix != prefix_buffer) {
            strcpy_s(prefix_buffer, sizeof(prefix_buffer), config_.twitch_command_prefix.c_str());
        }
        
        if (ImGui::InputText("Command Prefix", prefix_buffer, sizeof(prefix_buffer))) {
            config_.twitch_command_prefix = prefix_buffer;
            SaveConfig();
        }
        
        static char lock_cmd_buffer[64] = "";
        // Always sync buffer with current config value
        if (config_.twitch_lock_command != lock_cmd_buffer) {
            strcpy_s(lock_cmd_buffer, sizeof(lock_cmd_buffer), config_.twitch_lock_command.c_str());
        }
        
        if (ImGui::InputText("Lock Command", lock_cmd_buffer, sizeof(lock_cmd_buffer))) {
            config_.twitch_lock_command = lock_cmd_buffer;
            SaveConfig();
        }
        
        static char unlock_cmd_buffer[64] = "";
        // Always sync buffer with current config value
        if (config_.twitch_unlock_command != unlock_cmd_buffer) {
            strcpy_s(unlock_cmd_buffer, sizeof(unlock_cmd_buffer), config_.twitch_unlock_command.c_str());
        }
        
        if (ImGui::InputText("Unlock Command", unlock_cmd_buffer, sizeof(unlock_cmd_buffer))) {
            config_.twitch_unlock_command = unlock_cmd_buffer;
            SaveConfig();
        }
        
        static char status_cmd_buffer[64] = "";
        // Always sync buffer with current config value
        if (config_.twitch_status_command != status_cmd_buffer) {
            strcpy_s(status_cmd_buffer, sizeof(status_cmd_buffer), config_.twitch_status_command.c_str());
        }
        
        if (ImGui::InputText("Status Command", status_cmd_buffer, sizeof(status_cmd_buffer))) {
            config_.twitch_status_command = status_cmd_buffer;
            SaveConfig();
        }
        
        ImGui::EndDisabled();
        
        ImGui::Separator();
        
        // Donation Trigger Settings
        ImGui::Text("Donation Trigger Settings:");
        ImGui::TextWrapped("Configure which viewer actions can trigger device locking. These provide security by requiring financial contribution.");
        
        bool bits_enabled = config_.twitch_bits_enabled;
        if (ImGui::Checkbox("Enable Bits/Cheering Triggers", &bits_enabled)) {
            config_.twitch_bits_enabled = bits_enabled;
            SaveConfig();
        }
        
        ImGui::BeginDisabled(!config_.twitch_bits_enabled);
        int bits_minimum = config_.twitch_bits_minimum;
        if (ImGui::InputInt("Minimum Bits", &bits_minimum)) {
            config_.twitch_bits_minimum = (std::max)(1, bits_minimum);
            SaveConfig();
        }
        ImGui::EndDisabled();
        
        bool subs_enabled = config_.twitch_subs_enabled;
        if (ImGui::Checkbox("Enable Subscription Triggers", &subs_enabled)) {
            config_.twitch_subs_enabled = subs_enabled;
            SaveConfig();
        }
        
        bool donations_enabled = config_.twitch_donations_enabled;
        if (ImGui::Checkbox("Enable Donation Triggers", &donations_enabled)) {
            config_.twitch_donations_enabled = donations_enabled;
            SaveConfig();
        }
        
        ImGui::BeginDisabled(!config_.twitch_donations_enabled);
        float donation_minimum = config_.twitch_donation_minimum;
        if (ImGui::InputFloat("Minimum Donation ($)", &donation_minimum)) {
            config_.twitch_donation_minimum = (std::max)(0.01f, donation_minimum);
            SaveConfig();
        }
        ImGui::EndDisabled();
        
        ImGui::Separator();
        
        // Lock Duration Settings
        ImGui::Text("Lock Duration Settings:");
        
        bool duration_enabled = config_.twitch_lock_duration_enabled;
        if (ImGui::Checkbox("Enable Dynamic Lock Duration", &duration_enabled)) {
            config_.twitch_lock_duration_enabled = duration_enabled;
            SaveConfig();
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("When enabled, lock duration scales with donation amount");
            ImGui::EndTooltip();
        }
        
        ImGui::BeginDisabled(!config_.twitch_lock_duration_enabled);
        
        float base_duration = config_.twitch_lock_base_duration;
        if (ImGui::SliderFloat("Base Duration", &base_duration, 10.0f, 300.0f, "%.0f seconds")) {
            config_.twitch_lock_base_duration = base_duration;
            SaveConfig();
        }
        
        float per_dollar = config_.twitch_lock_per_dollar;
        if (ImGui::SliderFloat("Per Dollar/100 Bits", &per_dollar, 1.0f, 120.0f, "%.0f seconds")) {
            config_.twitch_lock_per_dollar = per_dollar;
            SaveConfig();
        }
        
        float max_duration = config_.twitch_lock_max_duration;
        if (ImGui::SliderFloat("Maximum Duration", &max_duration, 60.0f, 3600.0f, "%.0f seconds")) {
            config_.twitch_lock_max_duration = max_duration;
            SaveConfig();
        }
        
        ImGui::EndDisabled();
        
        ImGui::Separator();
        
        // Device Targeting
        ImGui::Text("Device Targeting:");
        ImGui::TextWrapped("Choose which devices can be locked by Twitch triggers.");
        
        bool target_all = config_.twitch_target_all_devices;
        if (ImGui::Checkbox("Target All Devices", &target_all)) {
            config_.twitch_target_all_devices = target_all;
            SaveConfig();
        }
        
        ImGui::BeginDisabled(config_.twitch_target_all_devices);
        
        bool target_hmd = config_.twitch_target_hmd;
        if (ImGui::Checkbox("HMD", &target_hmd)) {
            config_.twitch_target_hmd = target_hmd;
            SaveConfig();
        }
        
        ImGui::SameLine();
        bool target_left_hand = config_.twitch_target_left_hand;
        if (ImGui::Checkbox("Left Hand", &target_left_hand)) {
            config_.twitch_target_left_hand = target_left_hand;
            SaveConfig();
        }
        
        ImGui::SameLine();
        bool target_right_hand = config_.twitch_target_right_hand;
        if (ImGui::Checkbox("Right Hand", &target_right_hand)) {
            config_.twitch_target_right_hand = target_right_hand;
            SaveConfig();
        }
        
        bool target_left_foot = config_.twitch_target_left_foot;
        if (ImGui::Checkbox("Left Foot", &target_left_foot)) {
            config_.twitch_target_left_foot = target_left_foot;
            SaveConfig();
        }
        
        ImGui::SameLine();
        bool target_right_foot = config_.twitch_target_right_foot;
        if (ImGui::Checkbox("Right Foot", &target_right_foot)) {
            config_.twitch_target_right_foot = target_right_foot;
            SaveConfig();
        }
        
        ImGui::SameLine();
        bool target_hip = config_.twitch_target_hip;
        if (ImGui::Checkbox("Hip", &target_hip)) {
            config_.twitch_target_hip = target_hip;
            SaveConfig();
        }
        
        ImGui::EndDisabled();
        
        ImGui::Separator();
        
        // OAuth Setup and Test Buttons
        ImGui::Text("OAuth Setup:");
        ImGui::BeginDisabled(!config_.twitch_enabled || config_.twitch_client_id.empty() || config_.twitch_client_secret.empty());
        
        // OAuth Status
        static std::string oauth_url = "";
        static bool oauth_server_running = false;
        
        if (twitch_manager_) {
            // Check if we already have tokens
            if (!config_.twitch_access_token.empty()) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✅ OAuth tokens available");
                
                if (ImGui::Button("Test Connection")) {
                    twitch_manager_->ConnectToTwitch();
                }
                
                ImGui::SameLine();
                if (ImGui::Button("Clear Tokens")) {
                    config_.twitch_access_token.clear();
                    config_.twitch_refresh_token.clear();
                    SaveConfig();
                }
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "⚠️ OAuth setup required");
                
                if (!oauth_server_running) {
                    if (ImGui::Button("Start OAuth Setup")) {
                        // Start the OAuth server and generate URL
                        twitch_manager_->StartOAuthServer();
                        oauth_url = twitch_manager_->GenerateOAuthURL();
                        oauth_server_running = true;
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "🌐 OAuth server running on localhost:8080");
                    
                    if (ImGui::Button("Stop OAuth Server")) {
                        twitch_manager_->StopOAuthServer();
                        oauth_server_running = false;
                        oauth_url.clear();
                    }
                }
            }
        }
        
        // Show OAuth URL if available
        if (oauth_server_running && !oauth_url.empty()) {
            ImGui::Spacing();
            ImGui::TextWrapped("Step 1: Click the button below to open Twitch authorization in your browser:");
            
            if (ImGui::Button("🌐 Open Twitch Authorization", ImVec2(300, 30))) {
                // Open URL in default browser
                std::string command = "start \"\" \"" + oauth_url + "\"";
                system(command.c_str());
            }
            
            ImGui::Spacing();
            ImGui::TextWrapped("Step 2: Authorize the application in your browser. You'll be redirected automatically and this will complete the setup!");
            
            ImGui::Spacing();
            ImGui::Text("OAuth URL (for manual copy if needed):");
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
            ImGui::InputTextMultiline("##oauth_url", const_cast<char*>(oauth_url.c_str()), oauth_url.length(), 
                                     ImVec2(-1, 60), ImGuiInputTextFlags_ReadOnly);
            ImGui::PopStyleColor();
            
            if (ImGui::Button("Copy URL to Clipboard")) {
                ImGui::SetClipboardText(oauth_url.c_str());
            }
        }
        
        ImGui::EndDisabled();
        
        ImGui::Separator();
        
        // Test Buttons
        ImGui::Text("Test Functions:");
        ImGui::BeginDisabled(!config_.twitch_enabled || !twitch_manager_ || !twitch_manager_->IsConnected());
        
        if (ImGui::Button("Test Chat Message")) {
            if (twitch_manager_) {
                twitch_manager_->TestChatMessage();
            }
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Test Donation Lock")) {
            if (twitch_manager_) {
                twitch_manager_->TestDonationEvent("TestUser", 5.0f);
            }
        }
        
        ImGui::EndDisabled();
        
        ImGui::EndDisabled(); // End of user_agreement disabled block
    }

    // Twitch Helper Methods Implementation
    void UIManager::InitializeTwitchManager() {
        twitch_manager_ = std::make_unique<TwitchManager>();
        
        if (twitch_manager_->Initialize(&config_)) {
            // Set up callbacks for Twitch events
            twitch_manager_->SetDonationCallback(
                [this](const std::string& username, float amount, const std::string& message) {
                    OnTwitchDonation(username, amount, message);
                }
            );
            
            twitch_manager_->SetBitsCallback(
                [this](const std::string& username, int bits, const std::string& message) {
                    OnTwitchBits(username, bits, message);
                }
            );
            
            twitch_manager_->SetSubscriptionCallback(
                [this](const std::string& username, int months, bool is_gift) {
                    OnTwitchSubscription(username, months, is_gift);
                }
            );
            
            // Set up chat command callback to handle lock/unlock commands
            twitch_manager_->SetChatCommandCallback(
                [this](const std::string& username, const std::string& command, const std::string& args) {
                    OnTwitchChatCommand(username, command, args);
                }
            );
            
            if (Logger::IsInitialized()) {
                Logger::Info("TwitchManager initialized successfully");
            }
        } else {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to initialize TwitchManager");
            }
        }
    }

    void UIManager::ShutdownTwitchManager() {
        if (twitch_manager_) {
            twitch_manager_->Shutdown();
            twitch_manager_.reset();
            
            if (Logger::IsInitialized()) {
                Logger::Info("TwitchManager shut down");
            }
        }
    }

    void UIManager::OnTwitchDonation(const std::string& username, float amount, const std::string& message) {
        if (!config_.twitch_enabled) {
            return;
        }
        
        if (Logger::IsInitialized()) {
            Logger::Info("Processing Twitch donation from " + username + ": $" + std::to_string(amount));
        }
        
        // Check if donation meets minimum requirement
        if (amount < config_.twitch_donation_minimum) {
            if (Logger::IsInitialized()) {
                Logger::Info("Donation amount below minimum threshold");
            }
            return;
        }
        
        // Calculate lock duration if dynamic duration is enabled
        float lock_duration = 0.0f;
        if (config_.twitch_lock_duration_enabled) {
            lock_duration = config_.twitch_lock_base_duration + (amount * config_.twitch_lock_per_dollar);
            lock_duration = (std::min)(lock_duration, config_.twitch_lock_max_duration);
        } else {
            lock_duration = config_.unlock_timer_duration; // Use default unlock timer duration
        }
        
        // Activate appropriate device locks
        if (config_.twitch_target_all_devices) {
            ActivateGlobalLock(true);
            
            if (config_.twitch_chat_enabled && twitch_manager_ && twitch_manager_->IsConnected()) {
                twitch_manager_->SendChatMessage("@" + username + " Thank you for the $" + std::to_string(amount) + 
                                    " donation! All devices locked for " + std::to_string((int)lock_duration) + " seconds!");
            }
        } else {
            // Lock specific devices based on targeting settings
            int locked_count = 0;
            for (auto& device : device_positions_) {
                bool should_lock = false;
                
                switch (device.role) {
                    case DeviceRole::HMD:
                        should_lock = config_.twitch_target_hmd;
                        break;
                    case DeviceRole::LeftController:
                        should_lock = config_.twitch_target_left_hand;
                        break;
                    case DeviceRole::RightController:
                        should_lock = config_.twitch_target_right_hand;
                        break;
                    case DeviceRole::LeftFoot:
                        should_lock = config_.twitch_target_left_foot;
                        break;
                    case DeviceRole::RightFoot:
                        should_lock = config_.twitch_target_right_foot;
                        break;
                    case DeviceRole::Hip:
                        should_lock = config_.twitch_target_hip;
                        break;
                    default:
                        should_lock = false;
                        break;
                }
                
                if (should_lock) {
                    LockDevicePosition(device.serial, true);
                    locked_count++;
                }
            }
            
            if (config_.twitch_chat_enabled && twitch_manager_ && twitch_manager_->IsConnected()) {
                twitch_manager_->SendChatMessage("@" + username + " Thank you for the $" + std::to_string(amount) + 
                                    " donation! " + std::to_string(locked_count) + " devices locked for " + 
                                    std::to_string((int)lock_duration) + " seconds!");
            }
        }
        
        // Start unlock timer if enabled
        if (config_.unlock_timer_enabled && lock_duration > 0) {
            twitch_unlock_timer_active_ = true;
            twitch_unlock_timer_remaining_ = lock_duration;
            twitch_unlock_timer_start_ = std::chrono::steady_clock::now();
        }
    }

    void UIManager::OnTwitchBits(const std::string& username, int bits, const std::string& message) {
        if (!config_.twitch_enabled || !config_.twitch_bits_enabled) {
            return;
        }
        
        if (bits < config_.twitch_bits_minimum) {
            return;
        }
        
        // Convert bits to dollar equivalent for lock duration calculation
        float dollar_equivalent = bits / 100.0f; // 100 bits = $1
        OnTwitchDonation(username, dollar_equivalent, message);
    }

    void UIManager::OnTwitchSubscription(const std::string& username, int months, bool is_gift) {
        if (!config_.twitch_enabled || !config_.twitch_subs_enabled) {
            return;
        }
        
        // Treat subscription as a $5 donation for lock duration calculation
        float sub_value = is_gift ? 10.0f : 5.0f; // Gift subs worth more
        OnTwitchDonation(username, sub_value, is_gift ? "Gift subscription!" : "Subscription!");
    }

    void UIManager::ProcessTwitchUnlockTimer() {
        if (!twitch_unlock_timer_active_) {
            return;
        }
        
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - twitch_unlock_timer_start_).count() / 1000.0f;
        
        twitch_unlock_timer_remaining_ = (std::max)(0.0f, twitch_unlock_timer_remaining_ - elapsed);
        twitch_unlock_timer_start_ = current_time;
        
        // Check for audio warnings
        if (config_.unlock_timer_audio_warnings) {
            if ((twitch_unlock_timer_remaining_ <= 60.0f && twitch_unlock_timer_remaining_ > 59.0f) ||
                (twitch_unlock_timer_remaining_ <= 30.0f && twitch_unlock_timer_remaining_ > 29.0f) ||
                (twitch_unlock_timer_remaining_ <= 10.0f && twitch_unlock_timer_remaining_ > 9.0f)) {
                
                // Play warning sound (using existing audio system)
                if (config_.audio_enabled) {
                    AudioManager::PlayWarningSound(config_.audio_volume);
                }
                
                // Send chat message if enabled
                if (config_.twitch_chat_enabled && twitch_manager_ && twitch_manager_->IsConnected()) {
                    twitch_manager_->SendChatMessage("⏰ " + std::to_string((int)twitch_unlock_timer_remaining_) + " seconds until unlock!");
                }
            }
        }
        
        // Check if timer has expired
        if (twitch_unlock_timer_remaining_ <= 0.0f) {
            twitch_unlock_timer_active_ = false;
            
            // Unlock all devices
            ActivateGlobalLock(false);
            
            // Reset individual locks as well
            for (auto& device : device_positions_) {
                if (device.locked) {
                    LockDevicePosition(device.serial, false);
                }
            }
            
            if (config_.twitch_chat_enabled && twitch_manager_ && twitch_manager_->IsConnected()) {
                twitch_manager_->SendChatMessage("🔓 Timer expired - all devices unlocked!");
            }
            
            if (Logger::IsInitialized()) {
                Logger::Info("Twitch unlock timer expired - all devices unlocked");
            }
        }
    }

    void UIManager::OnTwitchChatCommand(const std::string& username, const std::string& command, const std::string& args) {
        if (!config_.twitch_enabled || !config_.twitch_chat_enabled) {
            return;
        }
        
        if (Logger::IsInitialized()) {
            Logger::Info("Processing Twitch chat command '" + command + "' from " + username);
        }
        
        // Handle lock command - equivalent to clicking "Lock All Included Devices"
        if (command == config_.twitch_lock_command) {
            // Count devices that will be locked
            int devices_to_lock = 0;
            for (const auto& device : device_positions_) {
                if (device.include_in_locking) {
                    devices_to_lock++;
                }
            }
            
            if (devices_to_lock == 0) {
                if (Logger::IsInitialized()) {
                    Logger::Warning("No devices selected for locking via chat command");
                }
                return;
            }
            
            if (Logger::IsInitialized()) {
                Logger::Info("Executing global lock via chat command - " + std::to_string(devices_to_lock) + " devices will be locked");
            }
            
            // Use the same method as the UI button
            ActivateGlobalLock(true);
            
        }
        // Handle unlock command - equivalent to clicking "Unlock All Included Devices"  
        else if (command == config_.twitch_unlock_command) {
            if (Logger::IsInitialized()) {
                Logger::Info("Executing global unlock via chat command");
            }
            
            // Use the same method as the UI button
            ActivateGlobalLock(false);
            
        }
        // Handle status command - report current lock state
        else if (command == config_.twitch_status_command) {
            int total_devices = 0;
            int included_devices = 0;
            int locked_devices = 0;
            
            for (const auto& device : device_positions_) {
                total_devices++;
                if (device.include_in_locking) {
                    included_devices++;
                    if (device.locked || global_lock_active_) {
                        locked_devices++;
                    }
                }
            }
            
            std::string status_message = "@" + username + " StayPutVR Status: " + 
                                       std::to_string(total_devices) + " devices detected, " +
                                       std::to_string(included_devices) + " included in locking";
            
            if (global_lock_active_) {
                status_message += ", GLOBAL LOCK ACTIVE (" + std::to_string(locked_devices) + " devices locked)";
            } else if (locked_devices > 0) {
                status_message += ", " + std::to_string(locked_devices) + " devices individually locked";
            } else {
                status_message += ", all devices unlocked";
            }
            
            // Send status response to chat
            if (twitch_manager_ && twitch_manager_->IsConnected()) {
                twitch_manager_->SendChatMessage(status_message);
            }
            
            if (Logger::IsInitialized()) {
                Logger::Info("Sent status response: " + status_message);
            }
        }
        else {
            if (Logger::IsInitialized()) {
                Logger::Warning("Unknown chat command: " + command);
            }
        }
    }

    void UIManager::TriggerGlobalOutOfBoundsActions() {
        if (Logger::IsInitialized()) {
            Logger::Info("Triggering global out-of-bounds actions with " + std::to_string(GLOBAL_OUT_OF_BOUNDS_DURATION) + "s timer");
        }
        
        // Start the timer for resetting back to normal state
        global_out_of_bounds_timer_active_ = true;
        global_out_of_bounds_timer_start_ = std::chrono::steady_clock::now();
        
        // Play out-of-bounds audio if enabled
        if (config_.out_of_bounds_audio) {
            std::string filePath = StayPutVR::GetAppDataPath() + "\\resources\\disobedience.wav";
            if (std::filesystem::exists(filePath)) {
                if (Logger::IsInitialized()) {
                    Logger::Debug("Playing disobedience.wav for global out-of-bounds trigger");
                }
                AudioManager::PlaySound("disobedience.wav", config_.audio_volume);
            } else {
                if (Logger::IsInitialized()) {
                    Logger::Warning("disobedience.wav not found, cannot play out-of-bounds sound");
                }
            }
        }
        
        // Trigger PiShock disobedience actions if enabled
        if (Logger::IsInitialized()) {
            Logger::Info("Triggering PiShock disobedience actions for global out-of-bounds");
        }
        TriggerPiShockDisobedience("GLOBAL");
        
        // Trigger OpenShock disobedience actions if enabled
        if (openshock_manager_ && openshock_manager_->IsEnabled()) {
            if (Logger::IsInitialized()) {
                Logger::Info("Triggering OpenShock disobedience actions for global out-of-bounds");
            }
            openshock_manager_->TriggerDisobedienceActions("GLOBAL");
        }
    }

    void UIManager::TriggerBiteActions() {
        if (Logger::IsInitialized()) {
            Logger::Info("Triggering bite disobedience actions with " + std::to_string(BITE_DURATION) + "s timer");
        }
        
        // Start the timer for resetting back to normal state
        bite_timer_active_ = true;
        bite_timer_start_ = std::chrono::steady_clock::now();
        
        // Play out-of-bounds audio if enabled
        if (config_.out_of_bounds_audio) {
            std::string filePath = StayPutVR::GetAppDataPath() + "\\resources\\disobedience.wav";
            if (std::filesystem::exists(filePath)) {
                if (Logger::IsInitialized()) {
                    Logger::Debug("Playing disobedience.wav for bite trigger");
                }
                AudioManager::PlaySound("disobedience.wav", config_.audio_volume);
            } else {
                if (Logger::IsInitialized()) {
                    Logger::Warning("disobedience.wav not found, cannot play bite sound");
                }
            }
        }
        
        // Trigger PiShock disobedience actions if enabled
        if (Logger::IsInitialized()) {
            Logger::Info("Triggering PiShock disobedience actions for bite");
        }
        TriggerPiShockDisobedience("BITE");
        
        // Trigger OpenShock disobedience actions if enabled
        if (openshock_manager_ && openshock_manager_->IsEnabled()) {
            if (Logger::IsInitialized()) {
                Logger::Info("Triggering OpenShock disobedience actions for bite");
            }
            openshock_manager_->TriggerDisobedienceActions("BITE");
        }
        
        // Update all device statuses to show out-of-bounds
        for (auto& device : device_positions_) {
            if (device.role != DeviceRole::None) {
                OSCDeviceType oscDevice = DeviceRoleToOSCDeviceType(device.role);
                UpdateDeviceStatus(oscDevice, DeviceStatus::LockedOutOfBounds);
                
                if (Logger::IsInitialized()) {
                    Logger::Debug("Updated device " + device.serial + " status to LockedOutOfBounds for bite trigger");
                }
            }
        }
    }

    void UIManager::ResetEmergencyStop() {
        if (!emergency_stop_active_) {
            return;
        }
        
        emergency_stop_active_ = false;
        
        if (Logger::IsInitialized()) {
            Logger::Info("Emergency stop mode reset - normal operation resumed");
        }
    }

    void UIManager::ProcessGlobalOutOfBoundsTimer() {
        if (!global_out_of_bounds_timer_active_) {
            return;
        }
        
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - global_out_of_bounds_timer_start_);
        float elapsed_seconds = elapsed.count() / 1000.0f;
        
        if (elapsed_seconds >= GLOBAL_OUT_OF_BOUNDS_DURATION) {
            // Timer expired, reset devices back to normal locked state
            global_out_of_bounds_timer_active_ = false;
            
            if (Logger::IsInitialized()) {
                Logger::Info("Global out-of-bounds timer expired, resetting devices to their normal states");
            }
            
            // Reset all devices back to their appropriate state (locked or unlocked)
            for (auto& device : device_positions_) {
                if (device.role != DeviceRole::None) {
                    OSCDeviceType oscDevice = DeviceRoleToOSCDeviceType(device.role);
                    
                    // Determine if device should be locked (either globally or individually)
                    bool should_be_locked = (device.include_in_locking && global_lock_active_) || device.locked;
                    
                    DeviceStatus newStatus;
                    
                    if (should_be_locked) {
                        // Device should be locked - determine appropriate locked status
                        newStatus = DeviceStatus::LockedSafe;
                        
                        // If the device is actually still out of bounds physically, keep it as disobedience
                        if (device.exceeds_threshold) {
                            newStatus = DeviceStatus::LockedDisobedience;
                        } else if (device.in_warning_zone) {
                            newStatus = DeviceStatus::LockedWarning;
                        }
                    } else {
                        // Device should be unlocked
                        newStatus = DeviceStatus::Unlocked;
                    }
                    
                    UpdateDeviceStatus(oscDevice, newStatus);
                    
                    if (Logger::IsInitialized()) {
                        Logger::Debug("Reset device " + device.serial + " to status: " + 
                                     std::to_string(static_cast<int>(newStatus)) + 
                                     " (should_be_locked=" + std::to_string(should_be_locked) + ")");
                    }
                }
            }
        }
    }

    void UIManager::ProcessBiteTimer() {
        if (!bite_timer_active_) {
            return;
        }
        
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - bite_timer_start_);
        float elapsed_seconds = elapsed.count() / 1000.0f;
        
        if (elapsed_seconds >= BITE_DURATION) {
            // Timer expired, reset devices back to normal locked state
            bite_timer_active_ = false;
            
            if (Logger::IsInitialized()) {
                Logger::Info("Bite timer expired, resetting devices to their normal states");
            }
            
            // Reset all devices back to their appropriate state (locked or unlocked)
            for (auto& device : device_positions_) {
                if (device.role != DeviceRole::None) {
                    OSCDeviceType oscDevice = DeviceRoleToOSCDeviceType(device.role);
                    
                    // Determine if device should be locked (either globally or individually)
                    bool should_be_locked = (device.include_in_locking && global_lock_active_) || device.locked;
                    
                    DeviceStatus newStatus;
                    
                    if (should_be_locked) {
                        // Device should be locked - determine appropriate locked status
                        newStatus = DeviceStatus::LockedSafe;
                        
                        // If the device is actually still out of bounds physically, keep it as disobedience
                        if (device.exceeds_threshold) {
                            newStatus = DeviceStatus::LockedDisobedience;
                        } else if (device.in_warning_zone) {
                            newStatus = DeviceStatus::LockedWarning;
                        }
                    } else {
                        // Device should be unlocked
                        newStatus = DeviceStatus::Unlocked;
                    }
                    
                    UpdateDeviceStatus(oscDevice, newStatus);
                    
                    if (Logger::IsInitialized()) {
                        Logger::Debug("Reset device " + device.serial + " to status: " + 
                                     std::to_string(static_cast<int>(newStatus)) + 
                                     " (should_be_locked=" + std::to_string(should_be_locked) + ")");
                    }
                }
            }
        }
    }

    // OpenShock Helper Methods Implementation
    void UIManager::InitializeOpenShockManager() {
        openshock_manager_ = std::make_unique<OpenShockManager>();
        
        if (openshock_manager_->Initialize(&config_)) {
            // Set up callback for OpenShock action results
            openshock_manager_->SetActionCallback(
                [this](const std::string& action_type, bool success, const std::string& message) {
                    if (Logger::IsInitialized()) {
                        Logger::Info("OpenShock " + action_type + " " + (success ? "succeeded" : "failed") + 
                                   (message.empty() ? "" : ": " + message));
                    }
                }
            );
            
            if (Logger::IsInitialized()) {
                Logger::Info("OpenShockManager initialized successfully");
            }
        } else {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to initialize OpenShockManager");
            }
        }
    }

    void UIManager::ShutdownOpenShockManager() {
        if (openshock_manager_) {
            openshock_manager_->Shutdown();
            openshock_manager_.reset();
            
            if (Logger::IsInitialized()) {
                Logger::Info("OpenShockManager shut down");
            }
        }
    }

    void UIManager::RenderOpenShockTab() {
        ImGui::Text("OpenShock Integration");
        ImGui::Separator();
        
        // Safety warning (moved to the top)
        ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - 20);
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "WARNING: Safety Information");
        ImGui::Text("OpenShock should only be used in accordance with their safety instructions. The makers of StayPutVR accept and assume no liability for your usage of OpenShock, even if you use it in a manner you deem to be safe. This is for entertainment purposes only. When in doubt, use a low intensity and double-check all safety information, including safe placement of the device. The makers are not liable for any and all coding defects that may cause this feature to operate improperly. There is no express or implied guarantee that this feature will work properly.");
        ImGui::PopTextWrapPos();
        
        // Add agreement checkbox right after the disclaimer
        bool user_agreement = config_.openshock_user_agreement;
        if (ImGui::Checkbox("I understand and agree to the safety information above", &user_agreement)) {
            config_.openshock_user_agreement = user_agreement;
            SaveConfig();
        }
        
        ImGui::Separator();
        
        // Main enable/disable checkbox for OpenShock (disabled until agreement is checked)
        ImGui::BeginDisabled(!user_agreement);
        bool openshock_enabled = config_.openshock_enabled;
        if (ImGui::Checkbox("Enable OpenShock Integration", &openshock_enabled)) {
            config_.openshock_enabled = openshock_enabled;
            SaveConfig();
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Enable direct integration with OpenShock API for out-of-bounds enforcement");
            ImGui::EndTooltip();
        }
        
        // OpenShock API Credentials
        ImGui::Separator();
        ImGui::Text("OpenShock API Credentials:");
        
        static char api_token_buffer[256] = "";
        // Always sync the buffer with the current config value
        if (config_.openshock_api_token != api_token_buffer) {
            strcpy_s(api_token_buffer, sizeof(api_token_buffer), config_.openshock_api_token.c_str());
        }
        
        if (ImGui::InputText("API Token", api_token_buffer, sizeof(api_token_buffer), ImGuiInputTextFlags_Password)) {
            config_.openshock_api_token = api_token_buffer;
            SaveConfig();
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("API Token from your OpenShock account (found in Account settings)");
            ImGui::EndTooltip();
        }
        
        // Device IDs section
        ImGui::Text("Device IDs:");
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("OpenShock device IDs. Device 0 is considered the master device.\nLeave unused slots empty.");
            ImGui::EndTooltip();
        }
        
        static char device_id_buffers[5][128] = {"", "", "", "", ""};
        
        for (int i = 0; i < 5; ++i) {
            // Sync buffers with config values
            if (config_.openshock_device_ids[i] != device_id_buffers[i]) {
                strcpy_s(device_id_buffers[i], sizeof(device_id_buffers[i]), config_.openshock_device_ids[i].c_str());
            }
            
            std::string label = std::to_string(i) + ": ";
            if (i == 0) {
                label += "(master)";
            }
            
            ImGui::PushID(i);
            if (ImGui::InputText(label.c_str(), device_id_buffers[i], sizeof(device_id_buffers[i]))) {
                config_.openshock_device_ids[i] = device_id_buffers[i];
                SaveConfig();
            }
            ImGui::PopID();
        }
        
        static char server_url_buffer[256] = "";
        // Always sync the buffer with the current config value
        if (config_.openshock_server_url != server_url_buffer) {
            strcpy_s(server_url_buffer, sizeof(server_url_buffer), config_.openshock_server_url.c_str());
        }
        
        if (ImGui::InputText("Server URL", server_url_buffer, sizeof(server_url_buffer))) {
            config_.openshock_server_url = server_url_buffer;
            SaveConfig();
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("OpenShock server URL (default: https://api.openshock.app)");
            ImGui::EndTooltip();
        }
        
        ImGui::Separator();
        
        if (!config_.openshock_enabled) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        }
        
        // Warning Zone Actions
        ImGui::Text("Warning Zone Actions:");
        ImGui::Separator();
        
        ImGui::Text("Action Type:");
        int warning_action = config_.openshock_warning_action;
        
        if (ImGui::RadioButton("None##Warning", warning_action == 0)) {
            config_.openshock_warning_action = 0;
            SaveConfig();
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Shock##Warning", warning_action == 1)) {
            config_.openshock_warning_action = 1;
            SaveConfig();
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Vibrate##Warning", warning_action == 2)) {
            config_.openshock_warning_action = 2;
            SaveConfig();
        }
        
        // Warning Intensity Settings
        bool use_individual_warning = config_.openshock_use_individual_warning_intensities;
        if (ImGui::Checkbox("Use Individual Device Warning Intensities", &use_individual_warning)) {
            config_.openshock_use_individual_warning_intensities = use_individual_warning;
            SaveConfig();
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Enable to set different warning intensity levels for each OpenShock device. When disabled, all devices use the master warning intensity.");
            ImGui::EndTooltip();
        }
        
        if (!use_individual_warning) {
            // Master warning intensity slider
            float master_warning_intensity = config_.openshock_master_warning_intensity;
            if (ImGui::SliderFloat("Warning Intensity", &master_warning_intensity, 0.0f, 1.0f, "%.2f")) {
                config_.openshock_master_warning_intensity = master_warning_intensity;
                SaveConfig();
            }
            
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted("Master intensity level for warning zone actions (applies to all devices)");
                ImGui::EndTooltip();
            }
        } else {
            // Individual device warning intensity sliders
            ImGui::Text("Individual Device Warning Intensities:");
            ImGui::Indent();
            
            for (int i = 0; i < 5; ++i) {
                if (!config_.openshock_device_ids[i].empty()) {
                    ImGui::PushID(i);
                    
                    std::string warning_label = "Device " + std::to_string(i) + " Warning Intensity";
                    float individual_warning_intensity = config_.openshock_individual_warning_intensities[i];
                    
                    if (ImGui::SliderFloat(warning_label.c_str(), &individual_warning_intensity, 0.0f, 1.0f, "%.2f")) {
                        config_.openshock_individual_warning_intensities[i] = individual_warning_intensity;
                        SaveConfig();
                    }
                    
                    ImGui::PopID();
                }
            }
            
            ImGui::Unindent();
        }
        
        float warning_duration = config_.openshock_warning_duration;
        if (ImGui::SliderFloat("Warning Duration", &warning_duration, 0.0f, 1.0f, "%.2f")) {
            config_.openshock_warning_duration = warning_duration;
            SaveConfig();
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Duration for warning zone actions (0.0 = shortest, 1.0 = longest)");
            ImGui::EndTooltip();
        }
        
        ImGui::Separator();
        
        // Disobedience (Out of Bounds) Actions
        ImGui::Text("Disobedience (Out of Bounds) Actions:");
        ImGui::Separator();
        
        ImGui::Text("Action Type:");
        int disobedience_action = config_.openshock_disobedience_action;
        
        if (ImGui::RadioButton("None", disobedience_action == 0)) {
            config_.openshock_disobedience_action = 0;
            SaveConfig();
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Shock", disobedience_action == 1)) {
            config_.openshock_disobedience_action = 1;
            SaveConfig();
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Vibrate", disobedience_action == 2)) {
            config_.openshock_disobedience_action = 2;
            SaveConfig();
        }
        
        // Disobedience Intensity Settings
        bool use_individual_disobedience = config_.openshock_use_individual_disobedience_intensities;
        if (ImGui::Checkbox("Use Individual Device Disobedience Intensities", &use_individual_disobedience)) {
            config_.openshock_use_individual_disobedience_intensities = use_individual_disobedience;
            SaveConfig();
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Enable to set different disobedience intensity levels for each OpenShock device. When disabled, all devices use the master disobedience intensity.");
            ImGui::EndTooltip();
        }
        
        if (!use_individual_disobedience) {
            // Master disobedience intensity slider
            float master_disobedience_intensity = config_.openshock_master_disobedience_intensity;
            if (ImGui::SliderFloat("Disobedience Intensity", &master_disobedience_intensity, 0.0f, 1.0f, "%.2f")) {
                config_.openshock_master_disobedience_intensity = master_disobedience_intensity;
                SaveConfig();
            }
            
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted("Master intensity level for disobedience actions (applies to all devices)");
                ImGui::EndTooltip();
            }
        } else {
            // Individual device disobedience intensity sliders
            ImGui::Text("Individual Device Disobedience Intensities:");
            ImGui::Indent();
            
            for (int i = 0; i < 5; ++i) {
                if (!config_.openshock_device_ids[i].empty()) {
                    ImGui::PushID(i + 100); // Different ID range to avoid conflicts
                    
                    std::string disobedience_label = "Device " + std::to_string(i) + " Disobedience Intensity";
                    float individual_disobedience_intensity = config_.openshock_individual_disobedience_intensities[i];
                    
                    if (ImGui::SliderFloat(disobedience_label.c_str(), &individual_disobedience_intensity, 0.0f, 1.0f, "%.2f")) {
                        config_.openshock_individual_disobedience_intensities[i] = individual_disobedience_intensity;
                        SaveConfig();
                    }
                    
                    ImGui::PopID();
                }
            }
            
            ImGui::Unindent();
        }

        float disobedience_duration = config_.openshock_disobedience_duration;
        if (ImGui::SliderFloat("Disobedience Duration", &disobedience_duration, 0.0f, 1.0f, "%.2f")) {
            config_.openshock_disobedience_duration = disobedience_duration;
            SaveConfig();
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Duration for disobedience actions (0.0 = shortest, 1.0 = longest)");
            ImGui::EndTooltip();
        }
        
        if (!config_.openshock_enabled) {
            ImGui::PopStyleColor();
        }
        
        // Status and Test Section
        ImGui::Separator();
        ImGui::Text("Status:");
        
        if (openshock_manager_) {
            std::string status = openshock_manager_->GetConnectionStatus();
            
            if (status == "Ready") {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Status: %s", status.c_str());
            } else if (status == "Disabled") {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Status: %s", status.c_str());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Status: %s", status.c_str());
            }
            
            std::string last_error = openshock_manager_->GetLastError();
            if (!last_error.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Last Error: %s", last_error.c_str());
            }
        }
        
        // Test button
        ImGui::Separator();
        
        bool can_test = config_.openshock_enabled && 
                       openshock_manager_ && 
                       openshock_manager_->IsFullyConfigured();
        
        ImGui::BeginDisabled(!can_test);
        if (ImGui::Button("Test OpenShock Actions", ImVec2(200, 30))) {
            if (openshock_manager_) {
                openshock_manager_->TestActions();
            }
        }
        ImGui::EndDisabled();
        
        if (!can_test) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Configure OpenShock settings to enable testing");
        }
        
        ImGui::EndDisabled(); // End of the user_agreement disabled block
    }

} // namespace StayPutVR 