#include "UIManager.hpp"
#include <iostream>
#include <string>
#include <format>
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

using json = nlohmann::json;

extern std::atomic<bool> g_running;

namespace StayPutVR {

    UIManager::UIManager() : window_(nullptr), imgui_context_(nullptr), running_ptr_(&g_running) {
        // Initialize config_dir_ with AppData path
        config_dir_ = GetAppDataPath() + "\\config";
        // Initialize config_file_ with just the filename, not the full path
        config_file_ = "config.ini";
        // Increase window height to prevent cutting off UI elements
        window_height_ = 700;
        
        // Create device manager instance
        device_manager_ = new DeviceManager();
        
        // Initialize timestamps
        last_sound_time_ = std::chrono::steady_clock::now();
        last_pishock_time_ = std::chrono::steady_clock::now();
        last_osc_toggle_time_ = std::chrono::steady_clock::now();
    }

    UIManager::~UIManager() {
        Shutdown();
        
        // Clean up device manager
        if (device_manager_) {
            delete device_manager_;
            device_manager_ = nullptr;
        }
    }

    bool UIManager::Initialize() {
        // Setup GLFW error callback
        glfwSetErrorCallback(UIManager::GlfwErrorCallback);
        
        // Initialize GLFW
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
        
        glfwMakeContextCurrent(window_);
        glfwSwapInterval(1); // Enable vsync
        
        // Initialize GLAD
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cerr << "Failed to initialize GLAD" << std::endl;
            return false;
        }
        
        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        imgui_context_ = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
        
        // Setup Dear ImGui style
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
                
                OSCManager::GetInstance().SetGlobalLockCallback(
                    [this](bool lock) {
                        if (Logger::IsInitialized()) {
                            Logger::Info("Global " + std::string(lock ? "lock" : "unlock") + " triggered via OSC");
                        }
                        ActivateGlobalLock(lock);
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
        
        // Shutdown device manager and IPC connection first
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
        auto it = device_map_.find(serial);
        if (it != device_map_.end()) {
            size_t index = it->second;
            device_positions_[index].locked = lock;
            
            // If locking, store the current position as original
            if (lock) {
                for (int i = 0; i < 3; i++) device_positions_[index].original_position[i] = device_positions_[index].position[i];
                for (int i = 0; i < 4; i++) device_positions_[index].original_rotation[i] = device_positions_[index].rotation[i];
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
        global_lock_active_ = activate;
        
        // If activating, store current positions as original for all included devices
        if (activate) {
            for (auto& device : device_positions_) {
                if (device.include_in_locking) {
                    for (int i = 0; i < 3; i++) device.original_position[i] = device.position[i];
                    for (int i = 0; i < 4; i++) device.original_rotation[i] = device.rotation[i];
                    device.position_deviation = 0.0f;
                    device.exceeds_threshold = false;
                }
            }
            
            // Play lock sound if enabled
            if (config_.audio_enabled && config_.lock_audio) {
                AudioManager::PlayLockSound(config_.audio_volume);
            }
        } else {
            // Play unlock sound if enabled
            if (config_.audio_enabled && config_.unlock_audio) {
                AudioManager::PlayUnlockSound(config_.audio_volume);
            }
        }
    }
    
    void UIManager::CheckDevicePositionDeviations() {
        bool warning_triggered = false;
        bool out_of_bounds_triggered = false;
        bool success_triggered = false;
        bool disable_threshold_exceeded = false;
        
        // Get current time to check if we should play sound again
        auto current_time = std::chrono::steady_clock::now();
        
        for (auto& device : device_positions_) {
            if (device.include_in_locking && global_lock_active_) {
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
                        StayPutVR::Logger::Debug("Device returned to safe zone, triggering success sound");
                    }
                    success_triggered = true;
                }
                
                // Check for newly triggered PiShock events
                if (!was_warning && device.in_warning_zone) {
                    // Newly entered warning zone
                    if (StayPutVR::Logger::IsInitialized()) {
                        StayPutVR::Logger::Debug("Device " + device.serial + " entered warning zone, position deviation: " + 
                                                std::to_string(device.position_deviation));
                    }
                    
                    // Remove PiShock warning call - only use audio warnings for this zone
                    // Audio warning will still be handled by the existing code below
                }
                
                if (!was_exceeding && device.exceeds_threshold) {
                    // Newly entered out of bounds zone
                    if (StayPutVR::Logger::IsInitialized()) {
                        StayPutVR::Logger::Debug("Device " + device.serial + " entered out of bounds zone, position deviation: " + 
                                                std::to_string(device.position_deviation) + 
                                                ", was_exceeding=" + std::to_string(was_exceeding) + 
                                                ", pishock_enabled=" + std::to_string(config_.pishock_enabled));
                    }
                    
                    if (config_.pishock_enabled) {
                        if (StayPutVR::Logger::IsInitialized()) {
                            Logger::Info("Triggering initial PiShock disobedience actions for device " + device.serial);
                        }
                        SendPiShockDisobedienceActions();
                    }
                } 
                // Continue triggering PiShock for devices that remain in out-of-bounds zone
                else if (device.exceeds_threshold && config_.pishock_enabled) {
                    // Check if enough time has passed since the last PiShock action
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - last_pishock_time_).count();
                    
                    // Only send repeating PiShock actions every 2 seconds
                    if (elapsed >= 2) {
                        if (StayPutVR::Logger::IsInitialized()) {
                            Logger::Info("Triggering continuous PiShock disobedience actions for device " + device.serial + 
                                        " (" + std::to_string(elapsed) + " seconds since last action)");
                        }
                        SendPiShockDisobedienceActions();
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
                    // First disconnect if needed
                    device_manager_->Shutdown();
                    
                    // Then try to reconnect
                    if (device_manager_->Initialize()) {
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
        ImGui::Text("Lock Controls:");
        
        if (!global_lock_active_) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f)); // Green
            if (ImGui::Button("Lock Selected Devices", ImVec2(200, 40))) {
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
            if (ImGui::Button("Unlock All Devices", ImVec2(200, 40))) {
                ActivateGlobalLock(false);
            }
            ImGui::PopStyleColor();
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
        static int osc_receive_port = 9005;
        
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
            config_.osc_lock_path_hmd = "/avatar/parameters/SPVR_neck_enter";
            strcpy_s(hmd_path, sizeof(hmd_path), config_.osc_lock_path_hmd.c_str());
            
            config_.osc_lock_path_left_hand = "/avatar/parameters/SPVR_handLeft_enter";
            strcpy_s(left_hand_path, sizeof(left_hand_path), config_.osc_lock_path_left_hand.c_str());
            
            config_.osc_lock_path_right_hand = "/avatar/parameters/SPVR_handRight_enter";
            strcpy_s(right_hand_path, sizeof(right_hand_path), config_.osc_lock_path_right_hand.c_str());
            
            config_.osc_lock_path_left_foot = "/avatar/parameters/SPVR_footLeft_enter";
            strcpy_s(left_foot_path, sizeof(left_foot_path), config_.osc_lock_path_left_foot.c_str());
            
            config_.osc_lock_path_right_foot = "/avatar/parameters/SPVR_footRight_enter";
            strcpy_s(right_foot_path, sizeof(right_foot_path), config_.osc_lock_path_right_foot.c_str());
            
            config_.osc_lock_path_hip = "/avatar/parameters/SPVR_hip_enter";
            strcpy_s(hip_path, sizeof(hip_path), config_.osc_lock_path_hip.c_str());
            
            // Reset global lock/unlock paths
            config_.osc_global_lock_path = "/avatar/parameters/SPVR_global_lock";
            strcpy_s(global_lock_path, sizeof(global_lock_path), config_.osc_global_lock_path.c_str());
            
            config_.osc_global_unlock_path = "/avatar/parameters/SPVR_global_unlock";
            strcpy_s(global_unlock_path, sizeof(global_unlock_path), config_.osc_global_unlock_path.c_str());
            
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
        // Add more detailed logging
        if (Logger::IsInitialized()) {
            Logger::Debug("OnDeviceLocked called: device=" + GetOSCDeviceString(device) + 
                         ", locked=" + std::string(locked ? "true" : "false") + 
                         ", current_state=" + std::string(global_lock_active_ ? "locked" : "unlocked"));
        }
        
        // Only process "true" messages, ignore "false" messages
        if (!locked) {
            if (Logger::IsInitialized()) {
                Logger::Debug("OSC device unlock message received for " + GetOSCDeviceString(device) + " - ignoring");
            }
            return;
        }
        
        // Implement debouncing - check if enough time has passed since last action
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_osc_toggle_time_).count();
        
        // Require at least 1000ms (1 second) between toggles
        if (elapsed < 1000) {
            if (Logger::IsInitialized()) {
                Logger::Debug("OSC device lock message debounced for " + GetOSCDeviceString(device) + 
                             " - only " + std::to_string(elapsed) + "ms elapsed");
            }
            return;
        }
        
        // Update last toggle time
        last_osc_toggle_time_ = current_time;
        
        // Toggle the current global lock state
        bool new_state = !global_lock_active_;
        
        if (Logger::IsInitialized()) {
            Logger::Info("OSC device lock message received for " + GetOSCDeviceString(device) + 
                        ". Toggling global lock to " + std::string(new_state ? "ON" : "OFF"));
        }
        
        // Activate or deactivate global lock based on the new toggled state
        ActivateGlobalLock(new_state);
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
        ImGui::Text("Version: 1.0.0");
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
            ImGui::TableSetupColumn("Device Type");
            ImGui::TableSetupColumn("Serial");
            ImGui::TableSetupColumn("Custom Name");
            ImGui::TableSetupColumn("Position & Rotation");
            ImGui::TableSetupColumn("Status");
            ImGui::TableSetupColumn("Actions");
            ImGui::TableHeadersRow();
            
            for (auto& device : device_positions_) {
                ImGui::TableNextRow();
                
                // Device Type
                ImGui::TableNextColumn();
                const char* type_str = "Unknown";
                switch (device.type) {
                    case DeviceType::HMD: type_str = "HMD"; break;
                    case DeviceType::CONTROLLER: type_str = "Controller"; break;
                    case DeviceType::TRACKER: type_str = "Tracker"; break;
                    case DeviceType::TRACKING_REFERENCE: type_str = "Base Station"; break;
                }
                ImGui::Text("%s", type_str);
                
                // Serial
                ImGui::TableNextColumn();
                ImGui::Text("%s", device.serial.c_str());
                
                // Custom Name
                ImGui::TableNextColumn();
                
                // Create a unique ID for input
                ImGui::PushID(("deviceName" + device.serial).c_str());
                
                // Create a buffer for the device name
                char name_buffer[64] = "";
                if (!device.device_name.empty()) {
                    strcpy_s(name_buffer, sizeof(name_buffer), device.device_name.c_str());
                }
                
                // Input field for the device name
                if (ImGui::InputText("##DeviceName", name_buffer, sizeof(name_buffer))) {
                    device.device_name = name_buffer;
                    config_.device_names[device.serial] = name_buffer;
                    
                    // Save config immediately when a device name changes
                    if (StayPutVR::Logger::IsInitialized()) {
                        StayPutVR::Logger::Info("Device name changed: " + device.serial + " -> " + name_buffer);
                    }
                    SaveConfig();
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
                }
                
                // Actions column
                ImGui::TableNextColumn();
                ImGui::PushID(device.serial.c_str());
                
                // Replace checkbox with a toggleable button
                if (device.include_in_locking) {
                    // Green "Will Lock" button
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.9f, 0.3f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.7f, 0.1f, 1.0f));
                    
                    if (ImGui::Button("Will Lock", ImVec2(120, 30))) {
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
                    
                    if (ImGui::Button("Won't Lock", ImVec2(120, 30))) {
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
                    config_.osc_receive_port = 9005;
                }
                
                if (StayPutVR::Logger::IsInitialized()) {
                    StayPutVR::Logger::Info("UIManager: Config loaded successfully");
                }
            } else {
                // Set default OSC ports for new config
                config_.osc_send_port = 9000;
                config_.osc_receive_port = 9005;
                
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
        
        // Store device names and settings
        config_.device_names.clear();
        config_.device_settings.clear();
        
        for (const auto& device : device_positions_) {
            // Store device name if not empty
            if (!device.device_name.empty()) {
                config_.device_names[device.serial] = device.device_name;
            }
            
            // Store include_in_locking setting
            config_.device_settings[device.serial] = device.include_in_locking;
        }
        
        if (StayPutVR::Logger::IsInitialized()) {
            StayPutVR::Logger::Info("Updated configuration from UI for " + 
                                    std::to_string(device_positions_.size()) + " devices");
        }
    }

    void UIManager::UpdateUIFromConfig() {
        // Boundary settings
        warning_threshold_ = config_.warning_threshold;
        position_threshold_ = config_.bounds_threshold;
        disable_threshold_ = config_.disable_threshold;
        
        // Update OSC status
        osc_enabled_ = config_.osc_enabled;
        
        // Apply device names and settings
        for (auto& device : device_positions_) {
            // Apply device name if available
            auto nameIt = config_.device_names.find(device.serial);
            if (nameIt != config_.device_names.end()) {
                device.device_name = nameIt->second;
            }
            
            // Apply include_in_locking setting if available
            auto settingIt = config_.device_settings.find(device.serial);
            if (settingIt != config_.device_settings.end()) {
                device.include_in_locking = settingIt->second;
                
                if (StayPutVR::Logger::IsInitialized()) {
                    StayPutVR::Logger::Info("Applied include_in_locking setting for device: " + device.serial);
                }
            }
        }
    }

    void UIManager::RenderPiShockTab() {
        ImGui::Text("PiShock Integration");
        ImGui::Separator();
        
        // Safety warning (moved to the top)
        ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - 20);
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "WARNING: Safety Information");
        ImGui::Text("PiShock should only be used in accordance with their safety instructions. The makers of StayPutVR accept and assume no liability for your usage of PiShock, even if you use it in a manner you deem to be safe. This is for entertainment purposes only. When in doubt, use a low intensity and double-check all safety information, including safe placement of the device. The makers are not liable for any and all coding defects that may cause this feature to operate improperly. There is no express or implied guarantee that this feature will work properly.");
        ImGui::PopTextWrapPos();
        
        // Add agreement checkbox right after the disclaimer
        bool user_agreement = config_.pishock_user_agreement;
        if (ImGui::Checkbox("I understand and agree to the safety information above", &user_agreement)) {
            config_.pishock_user_agreement = user_agreement;
            SaveConfig();
        }
        
        ImGui::Separator();
        
        // Main enable/disable checkbox for PiShock (disabled until agreement is checked)
        ImGui::BeginDisabled(!user_agreement);
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
        
        // PiShock API Credentials (NEW)
        ImGui::Separator();
        ImGui::Text("PiShock API Credentials:");
        
        static char username_buffer[128] = "";
        if (strlen(username_buffer) == 0 && !config_.pishock_username.empty()) {
            strcpy_s(username_buffer, sizeof(username_buffer), config_.pishock_username.c_str());
        }
        
        if (ImGui::InputText("Username", username_buffer, sizeof(username_buffer))) {
            config_.pishock_username = username_buffer;
            SaveConfig();
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Username you use to log into PiShock.com");
            ImGui::EndTooltip();
        }
        
        static char apikey_buffer[128] = "";
        if (strlen(apikey_buffer) == 0 && !config_.pishock_api_key.empty()) {
            strcpy_s(apikey_buffer, sizeof(apikey_buffer), config_.pishock_api_key.c_str());
        }
        
        if (ImGui::InputText("API Key", apikey_buffer, sizeof(apikey_buffer), ImGuiInputTextFlags_Password)) {
            config_.pishock_api_key = apikey_buffer;
            SaveConfig();
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("API Key generated on PiShock.com (found in Account section)");
            ImGui::EndTooltip();
        }
        
        static char sharecode_buffer[128] = "";
        if (strlen(sharecode_buffer) == 0 && !config_.pishock_share_code.empty()) {
            strcpy_s(sharecode_buffer, sizeof(sharecode_buffer), config_.pishock_share_code.c_str());
        }
        
        if (ImGui::InputText("Share Code", sharecode_buffer, sizeof(sharecode_buffer), ImGuiInputTextFlags_Password)) {
            config_.pishock_share_code = sharecode_buffer;
            SaveConfig();
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Share Code generated on PiShock.com for the device you want to control");
            ImGui::EndTooltip();
        }
        
        ImGui::Separator();
        
        if (!config_.pishock_enabled) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        }
        
        // Removed Warning Zone Actions section

        // Out of Bounds (Disobedience) Actions
        ImGui::Text("Out of Bounds Actions:");
        ImGui::Separator();
        
        ImGui::BeginDisabled(!config_.pishock_enabled);
        
        ImGui::TextWrapped("PiShock will only be triggered when a device exceeds the out-of-bounds threshold. Warnings will only use audio cues.");
        ImGui::Spacing();
        
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
        
        float disobedience_intensity = config_.pishock_disobedience_intensity;
        if (ImGui::SliderFloat("Out of Bounds Intensity", &disobedience_intensity, 0.0f, 1.0f, "%.2f")) {
            config_.pishock_disobedience_intensity = disobedience_intensity;
            SaveConfig();
        }
        
        float disobedience_duration = config_.pishock_disobedience_duration;
        if (ImGui::SliderFloat("Out of Bounds Duration", &disobedience_duration, 0.0f, 1.0f, "%.2f")) {
            config_.pishock_disobedience_duration = disobedience_duration;
            SaveConfig();
        }
        
        ImGui::EndDisabled();
        
        if (!config_.pishock_enabled) {
            ImGui::PopStyleColor();
        }
        
        ImGui::Separator();
        
        // Test buttons - Only keeping test for out of bounds
        ImGui::Text("Test Buttons:");
        ImGui::BeginDisabled(!config_.pishock_enabled || config_.pishock_username.empty() || 
                             config_.pishock_api_key.empty() || config_.pishock_share_code.empty());
        
        if (ImGui::Button("Test Out of Bounds Actions")) {
            SendPiShockDisobedienceActions();
        }
        
        ImGui::EndDisabled();
        ImGui::EndDisabled(); // End of the user_agreement disabled block
    }

    void UIManager::SendPiShockDisobedienceActions() {
        if (Logger::IsInitialized()) {
            Logger::Debug("SendPiShockDisobedienceActions called - begin log tracking");
        }
        
        if (!config_.pishock_enabled || !config_.pishock_user_agreement ||
            config_.pishock_username.empty() || config_.pishock_api_key.empty() || 
            config_.pishock_share_code.empty()) {
            if (Logger::IsInitialized()) {
                Logger::Warning("PiShock disobedience actions skipped: not fully configured");
            }
            return;
        }
        
        // Rate limiting - only send every 2 seconds to avoid spamming
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - last_pishock_time_).count();
        if (elapsed < 2) {
            if (Logger::IsInitialized()) {
                Logger::Debug("PiShock disobedience actions skipped: rate limited, elapsed=" + 
                              std::to_string(elapsed) + " seconds since last action");
            }
            return;
        }
        
        // Update timestamp 
        last_pishock_time_ = current_time;
        
        // Calculate actual intensity and duration values
        // PiShock API requires values 1-100 for intensity and 1-15 for duration
        int intensity = static_cast<int>(config_.pishock_disobedience_intensity * 100.0f);
        int duration = static_cast<int>(config_.pishock_disobedience_duration * 15.0f);
        
        if (Logger::IsInitialized()) {
            Logger::Debug("PiShock disobedience settings: beep=" + 
                         std::to_string(config_.pishock_disobedience_beep) + 
                         ", vibrate=" + std::to_string(config_.pishock_disobedience_vibrate) + 
                         ", shock=" + std::to_string(config_.pishock_disobedience_shock) + 
                         ", intensity=" + std::to_string(intensity) + 
                         ", duration=" + std::to_string(duration));
        }
        
        // Send actions based on configuration asynchronously
        if (config_.pishock_disobedience_beep) {
            SendPiShockCommandAsync(
                config_.pishock_username, 
                config_.pishock_api_key, 
                config_.pishock_share_code, 
                2, // beep operation
                0, // intensity not used for beep
                duration,
                [](bool success, const std::string& response) {
                    if (Logger::IsInitialized()) {
                        Logger::Debug("PiShock disobedience beep result: " + 
                                     std::string(success ? "success" : "failed") + " - " + response);
                    }
                }
            );
        }
        
        if (config_.pishock_disobedience_vibrate) {
            SendPiShockCommandAsync(
                config_.pishock_username, 
                config_.pishock_api_key, 
                config_.pishock_share_code, 
                1, // vibrate operation
                intensity,
                duration,
                [](bool success, const std::string& response) {
                    if (Logger::IsInitialized()) {
                        Logger::Debug("PiShock disobedience vibrate result: " + 
                                     std::string(success ? "success" : "failed") + " - " + response);
                    }
                }
            );
        }
        
        if (config_.pishock_disobedience_shock) {
            SendPiShockCommandAsync(
                config_.pishock_username, 
                config_.pishock_api_key, 
                config_.pishock_share_code, 
                0, // shock operation
                intensity,
                duration,
                [](bool success, const std::string& response) {
                    if (Logger::IsInitialized()) {
                        Logger::Debug("PiShock disobedience shock result: " + 
                                     std::string(success ? "success" : "failed") + " - " + response);
                    }
                }
            );
        }
        
        if (Logger::IsInitialized()) {
            Logger::Info("Sent PiShock disobedience actions asynchronously");
        }
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
        
        OSCManager::GetInstance().SetGlobalLockCallback(
            [this](bool lock) {
                if (Logger::IsInitialized()) {
                    Logger::Info("Global " + std::string(lock ? "lock" : "unlock") + " triggered via OSC");
                }
                ActivateGlobalLock(lock);
            }
        );
        
        if (Logger::IsInitialized()) {
            Logger::Info("VerifyOSCCallbacks: OSC callbacks verified and re-registered");
        }
    }

} // namespace StayPutVR 