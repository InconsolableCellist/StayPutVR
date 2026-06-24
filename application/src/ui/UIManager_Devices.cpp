#include "UIManager.hpp"
#include "ImGuiHelpers.hpp"
#include <iostream>
#include <string>
#include <format>
#include <algorithm>
#include <cstdio>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include "stb/stb_image.h" // effigy PNG decode (impl is in stb_image_impl.cpp)
#include <nlohmann/json.hpp>
#include "../../common/Logger.hpp"
#include "../../common/PathUtils.hpp"
#include "../../common/Audio.hpp"
#ifdef _WIN32
#include <shellapi.h> // For ShellExecuteA
#else
#include <cstdlib> // For std::system (xdg-open)
#endif
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

                // Apply per-device shocker/vibrator bindings so the Visual view
                // and managers see the saved selection immediately.
                if (auto it2 = config_.device_pishock_ids.find(serial); it2 != config_.device_pishock_ids.end())
                    pos.pishock_enabled = it2->second;
                if (auto it2 = config_.device_openshock_ids.find(serial); it2 != config_.device_openshock_ids.end())
                    pos.openshock_enabled = it2->second;
                if (auto it2 = config_.device_vibration_ids.find(serial); it2 != config_.device_vibration_ids.end())
                    pos.vibration_device_enabled = it2->second;

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

                // Update movement heat (for device identification): fast attack
                // when moving, slow decay when still, so wiggling a tracker in
                // SteamVR makes its row light up and stay warm for a moment.
                {
                    float delta = 0.0f;
                    for (int i = 0; i < 3; ++i) {
                        delta += std::abs(device_positions_[index].position[i] -
                                          device_positions_[index].previous_position[i]);
                    }
                    const float kFullScaleDelta = 0.03f; // ~3 cm/update == full heat
                    float target = (std::min)(delta / kFullScaleDelta, 1.0f);
                    float& heat = device_positions_[index].movement_heat;
                    const float alpha = (target > heat) ? 0.6f : 0.04f; // fast attack, slow decay
                    heat += (target - heat) * alpha;
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

        // VRCFT JawOpen constraint runs every frame; it edge-detects the HMD
        // lock state itself (so it works for both global and individual locks).
        CheckJawOpenConstraint();

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
                
                // Trigger Buttplug safe zone actions for newly locked device
                if (buttplug_manager_ && buttplug_manager_->IsEnabled()) {
                    if (StayPutVR::Logger::IsInitialized()) {
                        Logger::Info("Triggering Buttplug safe zone actions for individually locked device " + serial);
                    }
                    buttplug_manager_->TriggerSafeZoneActions(serial);
                }
            } else {
                // Clear Buttplug zone state for unlocked device
                if (buttplug_manager_ && buttplug_manager_->IsEnabled()) {
                    if (StayPutVR::Logger::IsInitialized()) {
                        Logger::Info("Clearing Buttplug zone state for individually unlocked device " + serial);
                    }
                    buttplug_manager_->ClearZoneState(serial);
                }
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

    void UIManager::ActivateGlobalLock(bool activate, bool play_sound) {
        if (activate && config_.countdown_enabled) {
            // Start countdown by playing countdown.wav once
            // The countdown.wav is a 3-second sound
            if (config_.audio.enabled) {
                std::string filePath = StayPutVR::GetResourcesPath() + "/countdown.wav";
                if (std::filesystem::exists(filePath)) {
                    // Set timeout for the lock activation
                    countdown_active_ = true;
                    countdown_remaining_ = 3.5f; // 3.5 seconds to account for audio clip ending
                    countdown_last_beep_ = std::chrono::steady_clock::now();
                    
                    if (StayPutVR::Logger::IsInitialized()) {
                        StayPutVR::Logger::Info("Starting countdown with countdown sound");
                    }
                    
                    // Play the countdown sound once
                    AudioManager::PlaySound("countdown.wav", config_.audio.volume);
                } else {
                    // If countdown.wav doesn't exist, activate lock immediately
                    if (StayPutVR::Logger::IsInitialized()) {
                        StayPutVR::Logger::Warning("countdown.wav not found, locking immediately");
                    }
                    ActivateGlobalLockInternal(true, play_sound);
                }
            } else {
                // Audio disabled, activate lock immediately
                ActivateGlobalLockInternal(true, play_sound);
            }
            return; // Don't activate global lock yet, wait for countdown
        }
        
        // Direct activation/deactivation without countdown
        ActivateGlobalLockInternal(activate, play_sound);
    }

    // Internal method to actually handle the lock activation
    void UIManager::ActivateGlobalLockInternal(bool activate, bool play_sound) {
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
                    
                    // Trigger Buttplug safe zone actions if enabled (device starts in safe zone when locked)
                    if (buttplug_manager_ && buttplug_manager_->IsEnabled()) {
                        if (StayPutVR::Logger::IsInitialized()) {
                            Logger::Info("Triggering Buttplug safe zone actions for newly locked device " + device.serial);
                        }
                        buttplug_manager_->TriggerSafeZoneActions(device.serial);
                    }
                }
            }
            
            // Play lock sound if enabled
            if (play_sound && config_.audio.enabled && config_.audio.lock) {
                AudioManager::PlayLockSound(config_.audio.volume);
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
            
            // Clear all Buttplug zone states when unlocking
            if (buttplug_manager_ && buttplug_manager_->IsEnabled()) {
                if (StayPutVR::Logger::IsInitialized()) {
                    Logger::Info("Clearing Buttplug zone states for unlocked devices");
                }
                buttplug_manager_->ClearZoneState("ALL");
            }
            
            // Play unlock sound if enabled
            if (play_sound && config_.audio.enabled && config_.audio.unlock) {
                AudioManager::PlayUnlockSound(config_.audio.volume);
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
                    
                    // Trigger Buttplug safe zone actions when returning to safe zone
                    if (buttplug_manager_ && buttplug_manager_->IsEnabled()) {
                        if (StayPutVR::Logger::IsInitialized()) {
                            Logger::Info("Triggering Buttplug safe zone actions for device " + device.serial + " returning to safe zone");
                        }
                        buttplug_manager_->TriggerSafeZoneActions(device.serial);
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
                    
                    // Send OSC status update for warning zone entry
                    if (device.role != DeviceRole::None) {
                        OSCDeviceType oscDevice = DeviceRoleToOSCDeviceType(device.role);
                        UpdateDeviceStatus(oscDevice, DeviceStatus::LockedWarning);
                        
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Debug("Sent OSC status LockedWarning for device " + device.serial + 
                                                   " (role: " + OSCManager::GetInstance().GetRoleString(device.role) + ")");
                        }
                    }
                    
                    // Trigger Buttplug warning zone actions when entering warning zone
                    if (buttplug_manager_ && buttplug_manager_->IsEnabled()) {
                        if (StayPutVR::Logger::IsInitialized()) {
                            Logger::Info("Triggering Buttplug warning zone actions for device " + device.serial + " entering warning zone");
                        }
                        buttplug_manager_->TriggerWarningActions(device.serial);
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
                    
                    // Trigger Buttplug warning zone actions when returning to warning zone from out of bounds
                    if (buttplug_manager_ && buttplug_manager_->IsEnabled()) {
                        if (StayPutVR::Logger::IsInitialized()) {
                            Logger::Info("Triggering Buttplug warning zone actions for device " + device.serial + " returning to warning from out of bounds");
                        }
                        buttplug_manager_->TriggerWarningActions(device.serial);
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
        if (config_.audio.enabled) {
            // Special case for success sound: Always play immediately when triggered, 
            // regardless of cooldown or other playing sounds
            if (success_triggered) {
                std::string filePath = StayPutVR::GetResourcesPath() + "/success.wav";
                if (std::filesystem::exists(filePath)) {
                    if (StayPutVR::Logger::IsInitialized()) {
                        StayPutVR::Logger::Debug("Playing success.wav for return to safe zone");
                    }
                    // Stop any currently playing sound first
                    AudioManager::StopSound();
                    // Play the success sound with the current volume setting
                    AudioManager::PlaySound("success.wav", config_.audio.volume);
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
                if (out_of_bounds_triggered && config_.audio.out_of_bounds) {
                    std::string filePath = StayPutVR::GetResourcesPath() + "/disobedience.wav";
                    if (std::filesystem::exists(filePath)) {
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Debug("Playing disobedience.wav for out of bounds");
                        }
                        AudioManager::PlaySound("disobedience.wav", config_.audio.volume);
                        played_sound = true;
                    } else {
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Warning("disobedience.wav not found, cannot play out of bounds sound");
                        }
                    }
                }
                // Warning sound
                else if (warning_triggered && config_.audio.warning) {
                    AudioManager::PlayWarningSound(config_.audio.volume);
                    played_sound = true;
                }
                
                // If we played a sound, update the timestamp
                if (played_sound) {
                    last_sound_time_ = current_time;
                }
            }
        }
    }

    // VRCFT JawOpen constraint. The 1-D analog of CheckDevicePositionDeviations:
    // when the HMD locks, capture the jaw value as the baseline (after a grace
    // window), then enforce |current - baseline| against the warning/disobedience
    // margins, reusing the same punishment + audio + OSC-status pipeline.
    void UIManager::CheckJawOpenConstraint() {
        if (!config_.jawopen_enabled || emergency_stop_active_) {
            jaw_.active = false;
            jaw_.in_grace = false;
            jaw_.hmd_was_locked = false;
            jaw_.in_warning_zone = false;
            jaw_.exceeds_threshold = false;
            return;
        }

        // Is the HMD currently locked (individually or via a global lock)?
        bool hmd_locked = false;
        for (const auto& d : device_positions_) {
            if (d.role == DeviceRole::HMD &&
                (d.locked || (d.include_in_locking && global_lock_active_))) {
                hmd_locked = true;
                break;
            }
        }

        auto now = std::chrono::steady_clock::now();

        // Falling edge: HMD just unlocked -> deactivate.
        if (!hmd_locked) {
            if (jaw_.hmd_was_locked) {
                jaw_.active = false;
                jaw_.in_grace = false;
                jaw_.in_warning_zone = false;
                jaw_.exceeds_threshold = false;
                UpdateDeviceStatus(OSCDeviceType::Jaw, DeviceStatus::Unlocked);
            }
            jaw_.hmd_was_locked = false;
            return;
        }

        // Rising edge: HMD just locked -> start grace + baseline capture.
        if (!jaw_.hmd_was_locked) {
            jaw_.hmd_was_locked = true;
            jaw_.lock_time = now;
            jaw_.in_grace = true;
            jaw_.active = false;
            jaw_.baseline = jaw_.current;
            jaw_.in_warning_zone = false;
            jaw_.exceeds_threshold = false;
            UpdateDeviceStatus(OSCDeviceType::Jaw, DeviceStatus::LockedSafe);
        }

        // During the grace window keep re-sampling so the value the jaw settles
        // to becomes the ideal; suppress enforcement until it ends.
        if (jaw_.in_grace) {
            float since_lock = std::chrono::duration_cast<std::chrono::duration<float>>(
                now - jaw_.lock_time).count();
            jaw_.baseline = jaw_.current; // track latest
            if (since_lock >= config_.jawopen_grace_seconds) {
                jaw_.in_grace = false;
                jaw_.active = true;
                jaw_.baseline = jaw_.current; // freeze the ideal
            } else {
                return;
            }
        }

        // Enforcement: 1-D deviation from the captured baseline.
        jaw_.deviation = std::fabs(jaw_.current - jaw_.baseline);
        bool was_warning = jaw_.in_warning_zone;
        bool was_exceeding = jaw_.exceeds_threshold;
        bool was_safe = !was_warning && !was_exceeding;

        jaw_.exceeds_threshold = jaw_.deviation > config_.jawopen_disobedience_margin;
        jaw_.in_warning_zone = jaw_.deviation > config_.jawopen_warning_margin && !jaw_.exceeds_threshold;
        bool is_safe = !jaw_.exceeds_threshold && !jaw_.in_warning_zone;

        bool play_warning = false, play_diso = false, play_success = false;

        // Returned to safe zone.
        if (!was_safe && is_safe) {
            UpdateDeviceStatus(OSCDeviceType::Jaw, DeviceStatus::LockedSafe);
            if (buttplug_manager_ && buttplug_manager_->IsEnabled())
                buttplug_manager_->TriggerSafeZoneActions(kJawOpenSerial);
            play_success = true;
        }
        // Newly entered warning zone from safe (TriggerPiShockWarning also fires Buttplug warning).
        if (!was_warning && jaw_.in_warning_zone) {
            UpdateDeviceStatus(OSCDeviceType::Jaw, DeviceStatus::LockedWarning);
            TriggerPiShockWarning(kJawOpenSerial);
            play_warning = true;
        }
        // Returned from disobedience back to warning.
        if (was_exceeding && !jaw_.exceeds_threshold && jaw_.in_warning_zone) {
            UpdateDeviceStatus(OSCDeviceType::Jaw, DeviceStatus::LockedWarning);
        }
        // Newly entered disobedience zone.
        if (!was_exceeding && jaw_.exceeds_threshold) {
            UpdateDeviceStatus(OSCDeviceType::Jaw, DeviceStatus::LockedDisobedience);
            TriggerPiShockDisobedience(kJawOpenSerial);
            if (openshock_manager_ && openshock_manager_->IsEnabled())
                openshock_manager_->TriggerDisobedienceActions(kJawOpenSerial);
            play_diso = true;
        }
        // Continuous disobedience while out of range.
        else if (jaw_.exceeds_threshold && CanTriggerPiShock()) {
            TriggerPiShockDisobedience(kJawOpenSerial);
        }
        else if (jaw_.exceeds_threshold && openshock_manager_ && openshock_manager_->IsEnabled()) {
            if (openshock_manager_->CanTriggerAction())
                openshock_manager_->TriggerDisobedienceActions(kJawOpenSerial);
        }

        // Audio, sharing the position constraint's cooldown (last_sound_time_).
        if (config_.audio.enabled) {
            if (play_success) {
                std::string fp = StayPutVR::GetResourcesPath() + "/success.wav";
                if (std::filesystem::exists(fp)) {
                    AudioManager::StopSound();
                    AudioManager::PlaySound("success.wav", config_.audio.volume);
                    last_sound_time_ = now;
                    return;
                }
            }
            float elapsed = std::chrono::duration_cast<std::chrono::duration<float>>(
                now - last_sound_time_).count();
            if (elapsed >= 1.0f) {
                bool played = false;
                if (play_diso && config_.audio.out_of_bounds) {
                    std::string fp = StayPutVR::GetResourcesPath() + "/disobedience.wav";
                    if (std::filesystem::exists(fp)) {
                        AudioManager::PlaySound("disobedience.wav", config_.audio.volume);
                        played = true;
                    }
                } else if (play_warning && config_.audio.warning) {
                    AudioManager::PlayWarningSound(config_.audio.volume);
                    played = true;
                }
                if (played) last_sound_time_ = now;
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

    void UIManager::RenderZoneMap() {
        // Auto-fit the map to the available region so the rings never clip. The
        // largest threshold maps to the rim; device dots stay literal-distance
        // (clamped to the rim so a far-out device renders at the edge, not off
        // screen). Center = each device's locked origin.
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float canvas_size = (std::min)(avail.x, avail.y);
        if (canvas_size < 80.0f) canvas_size = 80.0f;

        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        ImVec2 canvas_center(canvas_pos.x + canvas_size / 2, canvas_pos.y + canvas_size / 2);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        const float margin_frac = 0.88f; // leave room for the zone labels
        float max_threshold = (std::max)((std::max)(warning_threshold_, position_threshold_), disable_threshold_);
        if (max_threshold < 1e-3f) max_threshold = 1e-3f;
        const float rim = canvas_size * 0.5f * margin_frac;
        const float scale_factor = rim / max_threshold; // largest ring == rim

        float disable_radius       = disable_threshold_ * scale_factor;
        float out_of_bounds_radius = position_threshold_ * scale_factor;
        float warning_radius       = warning_threshold_ * scale_factor;

        draw_list->AddCircleFilled(canvas_center, warning_radius, IM_COL32(0, 255, 0, 50));
        draw_list->AddCircle(canvas_center, disable_radius,       IM_COL32(255, 0, 0, 100), 0, 2.0f);
        draw_list->AddCircle(canvas_center, out_of_bounds_radius, IM_COL32(255, 128, 0, 150), 0, 2.0f);
        draw_list->AddCircle(canvas_center, warning_radius,       IM_COL32(255, 255, 0, 150), 0, 2.0f);

        for (const auto& device : device_positions_) {
            if (!(device.include_in_locking || device.locked)) continue;

            float deviation_x = 0.0f, deviation_z = 0.0f;
            if (device.locked || (device.include_in_locking && global_lock_active_)) {
                deviation_x = device.position[0] - device.original_position[0];
                deviation_z = device.position[2] - device.original_position[2];
            }

            float scaled_x = deviation_x * scale_factor;
            float scaled_z = deviation_z * scale_factor;
            float r = std::sqrt(scaled_x * scaled_x + scaled_z * scaled_z);
            if (r > rim && r > 0.0f) { scaled_x *= rim / r; scaled_z *= rim / r; } // clamp to rim, keep angle
            ImVec2 device_pos(canvas_center.x + scaled_x, canvas_center.y + scaled_z);

            float total_deviation = std::sqrt(deviation_x * deviation_x + deviation_z * deviation_z);
            ImU32 device_color =
                (total_deviation > position_threshold_) ? IM_COL32(255, 0, 0, 255) :
                (total_deviation > warning_threshold_)  ? IM_COL32(255, 255, 0, 255) :
                                                          IM_COL32(0, 255, 0, 255);

            draw_list->AddCircleFilled(device_pos, 5.0f, device_color);

            // Prefer the assigned role (HMD, R Hand, ...) over the raw device
            // name/serial so the map reads clearly.
            std::string label = ShortRoleName(device.role);
            if (label.empty()) label = device.device_name.empty() ? device.serial : device.device_name;
            draw_list->AddText(ImVec2(device_pos.x + 7, device_pos.y - 7), device_color, label.c_str());
        }

        draw_list->AddText(ImVec2(canvas_center.x - 25, canvas_center.y - warning_radius - 15),
                           IM_COL32(255, 255, 0, 255), "Warning");
        draw_list->AddText(ImVec2(canvas_center.x - 40, canvas_center.y - out_of_bounds_radius - 15),
                           IM_COL32(255, 128, 0, 255), "Out of Bounds");
        draw_list->AddText(ImVec2(canvas_center.x - 22, canvas_center.y - disable_radius - 15),
                           IM_COL32(255, 0, 0, 255), "Disable");

        ImGui::Dummy(ImVec2(canvas_size, canvas_size));
    }

    const char* UIManager::RoleName(DeviceRole role) {
        switch (role) {
            case DeviceRole::HMD: return "Collar / HMD";
            case DeviceRole::LeftController: return "Left Hand";
            case DeviceRole::RightController: return "Right Hand";
            case DeviceRole::Hip: return "Hip";
            case DeviceRole::LeftFoot: return "Left Foot";
            case DeviceRole::RightFoot: return "Right Foot";
            default: return "Unassigned";
        }
    }

    // Compact role label for the zone map / tight UI.
    const char* UIManager::ShortRoleName(DeviceRole role) {
        switch (role) {
            case DeviceRole::HMD: return "HMD";
            case DeviceRole::LeftController: return "L Hand";
            case DeviceRole::RightController: return "R Hand";
            case DeviceRole::Hip: return "Hip";
            case DeviceRole::LeftFoot: return "L Foot";
            case DeviceRole::RightFoot: return "R Foot";
            default: return "";
        }
    }

    std::string UIManager::SerialForRole(DeviceRole role) const {
        if (role == DeviceRole::None) return "";
        for (const auto& d : device_positions_)
            if (d.role == role) return d.serial;
        return "";
    }

    void UIManager::AssignRoleToSerial(const std::string& serial, DeviceRole role) {
        // Roles are unique slots: clear any other device currently holding it.
        if (role != DeviceRole::None) {
            for (auto& d : device_positions_) {
                if (d.serial != serial && d.role == role) {
                    d.role = DeviceRole::None;
                    config_.device_roles[d.serial] = static_cast<int>(DeviceRole::None);
                }
            }
        }
        for (auto& d : device_positions_) {
            if (d.serial == serial) {
                d.role = role;
                config_.device_roles[serial] = static_cast<int>(role);

                if (role != DeviceRole::None) {
                    // Default a newly-assigned device to participate in Lock All,
                    // unless the user has already chosen a setting for it.
                    if (config_.device_settings.find(serial) == config_.device_settings.end()) {
                        d.include_in_locking = true;
                        config_.device_settings[serial] = true;
                    }
                    // Default to all configured PiShock shockers, only on first
                    // assignment so manual edits stick.
                    if (config_.device_pishock_ids.find(serial) == config_.device_pishock_ids.end()) {
                        for (int i = 0; i < 5; ++i)
                            if (config_.pishock_shocker_ids[i] != 0) d.pishock_enabled[i] = true;
                        config_.device_pishock_ids[serial] = d.pishock_enabled;
                    }
                    // And to all configured OpenShock devices.
                    if (config_.device_openshock_ids.find(serial) == config_.device_openshock_ids.end()) {
                        for (int i = 0; i < 5; ++i)
                            if (!config_.openshock_device_ids[i].empty()) d.openshock_enabled[i] = true;
                        config_.device_openshock_ids[serial] = d.openshock_enabled;
                    }
                }
            }
        }
        SaveConfig();
    }

    void UIManager::LoadEffigyTexture() {
        if (effigy_load_attempted_) return;
        effigy_load_attempted_ = true;

        std::string path = GetResourcesPath() + "/effigy.png";
        if (!std::filesystem::exists(path)) {
            if (Logger::IsInitialized()) Logger::Info("UIManager: effigy.png not found; Visual tab uses a wireframe placeholder");
            return;
        }

        int w = 0, h = 0, n = 0;
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 4);
        if (!data) {
            if (Logger::IsInitialized()) Logger::Warning("UIManager: failed to decode effigy.png");
            return;
        }
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        stbi_image_free(data);

        effigy_tex_ = tex;
        effigy_tex_w_ = w;
        effigy_tex_h_ = h;
    }

    // Palette of draggable ID chips for the Visual tab, grouped by integration:
    // PiShock (blue) and OpenShock (red) bind independently even when they share a
    // slot number; Buttplug/BPIO (purple) is separate. Each category also has an
    // "All" chip. Drag a chip onto a body slot to bind that ID to the slot's
    // device. Payload code: category 'P'/'O'/'V' + index '0'..'4' or 'A' for all.
    void UIManager::RenderShockerPalette() {
        auto chip = [&](const char* label, const char* code, ImVec4 color) {
            ImGui::PushStyleColor(ImGuiCol_Button, color);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  ImVec4(color.x + 0.12f, color.y + 0.12f, color.z + 0.12f, 1.0f));
            ImGui::SmallButton(label);
            ImGui::PopStyleColor(2);
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                ImGui::SetDragDropPayload("SPVR_SHOCKID", code, 3);
                ImGui::Text("Bind %s", label);
                ImGui::EndDragDropSource();
            }
        };
        const ImVec4 blue(0.20f, 0.45f, 0.85f, 1.0f);
        const ImVec4 red(0.80f, 0.25f, 0.25f, 1.0f);
        const ImVec4 purple(0.55f, 0.30f, 0.80f, 1.0f);

        // One category row: a colored header, a chip per configured slot (0-based
        // label to match the PiShock/OpenShock panels), then an "All" chip.
        auto category = [&](const char* name, char cat, ImVec4 color, const char* prefix,
                            auto configured) {
            ImGui::TextColored(color, "%s:", name);
            bool any = false;
            for (int i = 0; i < 5; ++i) {
                if (!configured(i)) continue;
                any = true;
                ImGui::SameLine();
                char lbl[8]; std::snprintf(lbl, sizeof(lbl), "%s%d", prefix, i);
                char code[3] = { cat, (char)('0' + i), 0 };
                ImGui::PushID((int)cat * 100 + i);
                chip(lbl, code, color);
                ImGui::PopID();
            }
            if (any) {
                ImGui::SameLine();
                char code[3] = { cat, 'A', 0 };
                ImGui::PushID((int)cat * 100 + 99);
                chip("All", code, color);
                ImGui::PopID();
            } else {
                ImGui::SameLine();
                ImGui::TextDisabled("(none configured)");
            }
        };

        category("PiShock", 'P', blue, "S",
                 [&](int i){ return config_.pishock_shocker_ids[i] != 0; });
        category("OpenShock", 'O', red, "S",
                 [&](int i){ return !config_.openshock_device_ids[i].empty(); });
        category("BPIO", 'V', purple, "V",
                 [&](int i){ return config_.buttplug_device_indices[i] >= 0; });
    }

    // Bind (enable=true) or unbind (enable=false) a dragged ID chip (payload
    // code) on one device. Updates the device flags and the matching config map.
    void UIManager::ApplyIdBindingToDevice(DevicePosition& d, const char* code, bool enable) {
        if (!code) return;
        const char cat = code[0], sel = code[1];
        auto set = [&](std::array<bool, 5>& arr, auto configured) {
            if (sel == 'A') { for (int i = 0; i < 5; ++i) if (configured(i)) arr[i] = enable; }
            else { int i = sel - '0'; if (i >= 0 && i < 5 && configured(i)) arr[i] = enable; }
        };
        if (cat == 'P') {
            set(d.pishock_enabled, [&](int i){ return config_.pishock_shocker_ids[i] != 0; });
            config_.device_pishock_ids[d.serial] = d.pishock_enabled;
        } else if (cat == 'O') {
            set(d.openshock_enabled, [&](int i){ return !config_.openshock_device_ids[i].empty(); });
            config_.device_openshock_ids[d.serial] = d.openshock_enabled;
        } else if (cat == 'V') {
            set(d.vibration_device_enabled, [&](int i){ return config_.buttplug_device_indices[i] >= 0; });
            config_.device_vibration_ids[d.serial] = d.vibration_device_enabled;
        }
    }

    // Apply a dragged ID chip to the device assigned to a slot.
    void UIManager::ApplyIdBindingToRole(DeviceRole role, const char* code) {
        std::string serial = SerialForRole(role);
        if (serial.empty()) return;
        for (auto& x : device_positions_)
            if (x.serial == serial) { ApplyIdBindingToDevice(x, code, true); break; }
        SaveConfig();
    }

    // Apply/remove a dragged ID chip across every assigned cuff (device with a
    // role) at once.
    void UIManager::ApplyIdBindingToAllCuffs(const char* code, bool enable) {
        for (auto& x : device_positions_)
            if (x.role != DeviceRole::None) ApplyIdBindingToDevice(x, code, enable);
        SaveConfig();
    }

    // Bind a dragged ID chip to the JawOpen constraint. Mirrors
    // ApplyIdBindingToDevice but writes the jaw_ arrays and the reserved
    // kJawOpenSerial key so the standard Trigger*(serial) pipeline fires them.
    void UIManager::ApplyIdBindingToJaw(const char* code) {
        if (!code) return;
        const char cat = code[0], sel = code[1];
        auto set = [&](std::array<bool, 5>& arr, auto configured) {
            if (sel == 'A') { for (int i = 0; i < 5; ++i) if (configured(i)) arr[i] = true; }
            else { int i = sel - '0'; if (i >= 0 && i < 5 && configured(i)) arr[i] = true; }
        };
        if (cat == 'P') {
            set(jaw_.pishock_enabled, [&](int i){ return config_.pishock_shocker_ids[i] != 0; });
            config_.device_pishock_ids[kJawOpenSerial] = jaw_.pishock_enabled;
        } else if (cat == 'O') {
            set(jaw_.openshock_enabled, [&](int i){ return !config_.openshock_device_ids[i].empty(); });
            config_.device_openshock_ids[kJawOpenSerial] = jaw_.openshock_enabled;
        } else if (cat == 'V') {
            set(jaw_.vibration_device_enabled, [&](int i){ return config_.buttplug_device_indices[i] >= 0; });
            config_.device_vibration_ids[kJawOpenSerial] = jaw_.vibration_device_enabled;
        }
        SaveConfig();
    }

    // Populate the jaw_ binding arrays from the config maps (reserved serial),
    // mirroring the per-device binding load in UpdateDevicePositions.
    void UIManager::LoadJawBindingsFromConfig() {
        if (auto it = config_.device_pishock_ids.find(kJawOpenSerial); it != config_.device_pishock_ids.end())
            jaw_.pishock_enabled = it->second;
        if (auto it = config_.device_openshock_ids.find(kJawOpenSerial); it != config_.device_openshock_ids.end())
            jaw_.openshock_enabled = it->second;
        if (auto it = config_.device_vibration_ids.find(kJawOpenSerial); it != config_.device_vibration_ids.end())
            jaw_.vibration_device_enabled = it->second;
    }

    void UIManager::RenderVisualAssignment() {
        LoadEffigyTexture();

        struct Slot { DeviceRole role; const char* label; float ux, uy; };
        // Slot positions normalized to the effigy image (back-facing; viewer-left
        // = "Left" so the user's left tracker maps to the left of the screen).
        // Derived from pixel coords on the 630x1261 effigy.png:
        //   neck 290,240  CL 88,595  CR 482,606  FL 200,990  FR 346,999
        // Hip is intentionally omitted: it's unused and not exposed in the Unity
        // prefab. The role stays assignable via the device dropdown.
        static const Slot kSlots[] = {
            { DeviceRole::HMD,             "Collar/HMD", 0.460f, 0.190f },
            { DeviceRole::LeftController,  "L Hand",     0.140f, 0.472f },
            { DeviceRole::RightController, "R Hand",     0.765f, 0.481f },
            { DeviceRole::LeftFoot,        "L Foot",     0.317f, 0.785f },
            { DeviceRole::RightFoot,       "R Foot",     0.549f, 0.792f },
        };

        // Take all remaining vertical space; reserve room for the slot-config
        // panel below when a slot is selected.
        float avail_h = ImGui::GetContentRegionAvail().y;
        float config_h = (selected_slot_role_ != DeviceRole::None || jaw_selected_) ? 175.0f : 0.0f;
        float effigyH = avail_h - config_h - 8.0f;
        if (effigyH < 240.0f) effigyH = 240.0f;
        float effigyW = ImGui::GetContentRegionAvail().x * 0.50f;
        if (effigyW < 160.0f) effigyW = 160.0f;

        // ---- LEFT: effigy + slot hotspots ----
        ImGui::BeginChild("EffigyPane", ImVec2(effigyW, effigyH), true);
        {
            ImVec2 box = ImGui::GetContentRegionAvail();
            float imgH = box.y - 4.0f;
            float aspect = (effigy_tex_h_ > 0) ? (float)effigy_tex_w_ / (float)effigy_tex_h_ : 0.50f;
            float imgW = imgH * aspect;
            ImVec2 origin = ImGui::GetCursorScreenPos();
            ImVec2 paneOrigin = origin; // child top-left, before centering the image
            float xpad = (box.x - imgW) * 0.5f; if (xpad < 0.0f) xpad = 0.0f;
            origin.x += xpad;
            ImDrawList* dl = ImGui::GetWindowDrawList();

            // Outlined text so labels/IDs stay legible over the busy effigy art.
            auto drawOutlined = [&](ImVec2 p, ImU32 col, const char* t) {
                const ImU32 sh = IM_COL32(0, 0, 0, 230);
                dl->AddText(ImVec2(p.x + 1, p.y), sh, t);
                dl->AddText(ImVec2(p.x - 1, p.y), sh, t);
                dl->AddText(ImVec2(p.x, p.y + 1), sh, t);
                dl->AddText(ImVec2(p.x, p.y - 1), sh, t);
                dl->AddText(p, col, t);
            };

            if (effigy_tex_ != 0) {
                dl->AddImage((ImTextureID)(intptr_t)effigy_tex_, origin, ImVec2(origin.x + imgW, origin.y + imgH));
            } else {
                // Wireframe placeholder if the PNG is missing.
                ImU32 line = IM_COL32(100, 180, 255, 200);
                auto P = [&](float ux, float uy){ return ImVec2(origin.x + ux*imgW, origin.y + uy*imgH); };
                dl->AddCircle(P(0.49f,0.13f), imgW*0.10f, line, 24, 2.0f);
                dl->AddLine(P(0.49f,0.23f), P(0.49f,0.58f), line, 2.0f);
                dl->AddLine(P(0.30f,0.30f), P(0.70f,0.30f), line, 2.0f);
                dl->AddLine(P(0.30f,0.30f), P(0.16f,0.56f), line, 2.0f);
                dl->AddLine(P(0.70f,0.30f), P(0.84f,0.56f), line, 2.0f);
                dl->AddLine(P(0.42f,0.58f), P(0.42f,0.92f), line, 2.0f);
                dl->AddLine(P(0.59f,0.58f), P(0.59f,0.92f), line, 2.0f);
            }

            const float hot = 30.0f;
            for (const auto& s : kSlots) {
                ImVec2 c(origin.x + s.ux*imgW, origin.y + s.uy*imgH);
                ImGui::SetCursorScreenPos(ImVec2(c.x - hot/2, c.y - hot/2));
                ImGui::InvisibleButton(s.label, ImVec2(hot, hot));
                bool hovered = ImGui::IsItemHovered();

                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("SPVR_DEVICE")) {
                        std::string serial(static_cast<const char*>(p->Data));
                        AssignRoleToSerial(serial, s.role);
                    }
                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("SPVR_SHOCKID")) {
                        ApplyIdBindingToRole(s.role, static_cast<const char*>(p->Data));
                    }
                    ImGui::EndDragDropTarget();
                }
                if (ImGui::IsItemClicked()) selected_slot_role_ = s.role;

                // Colour the slot by the assigned device's live status (green safe,
                // yellow warning, red out-of-bounds; dim when unassigned).
                std::string serial = SerialForRole(s.role);
                bool filled = !serial.empty();
                ImU32 status_col = IM_COL32(130, 150, 170, 120); // unassigned (dim)
                if (filled) {
                    const DevicePosition* dev = nullptr;
                    for (const auto& d : device_positions_) if (d.serial == serial) { dev = &d; break; }
                    float dev_total = 0.0f;
                    if (dev && (dev->locked || (dev->include_in_locking && global_lock_active_))) {
                        float dx = dev->position[0] - dev->original_position[0];
                        float dz = dev->position[2] - dev->original_position[2];
                        dev_total = std::sqrt(dx*dx + dz*dz);
                    }
                    status_col = (dev_total > position_threshold_) ? IM_COL32(255, 70, 70, 255)
                               : (dev_total > warning_threshold_)  ? IM_COL32(255, 215, 60, 255)
                                                                   : IM_COL32(70, 220, 100, 255);
                }
                ImU32 ring = hovered ? IM_COL32(255, 255, 255, 255) : status_col;
                dl->AddCircleFilled(c, hot/2 - 2, filled ? IM_COL32(30, 60, 45, 150) : IM_COL32(20, 24, 32, 140));
                dl->AddCircle(c, hot/2, ring, 24, filled ? 3.0f : 1.5f);
                if (s.role == selected_slot_role_)
                    dl->AddCircle(c, hot/2 + 3, IM_COL32(80, 160, 255, 255), 24, 2.0f);

                // Label on the LEFT for left-side slots (L Hand / L Foot), else right.
                ImVec2 tsz = ImGui::CalcTextSize(s.label);
                bool label_left = (s.ux < 0.45f);
                ImVec2 tpos = label_left ? ImVec2(c.x - hot/2 - 3 - tsz.x, c.y - 7)
                                         : ImVec2(c.x + hot/2 + 3, c.y - 7);
                ImU32 label_col = filled ? status_col : IM_COL32(190, 200, 220, 230);
                drawOutlined(tpos, label_col, s.label);

                // Bound shocker/vibrator IDs drawn above the circle: blue = PiShock,
                // red = OpenShock, purple = BPIO.
                if (filled) {
                    const DevicePosition* dev = nullptr;
                    for (const auto& d : device_positions_) if (d.serial == serial) { dev = &d; break; }
                    if (dev) {
                        const ImU32 cb = IM_COL32(120, 190, 255, 255); // PiShock (bright blue)
                        const ImU32 cr = IM_COL32(255, 120, 120, 255); // OpenShock (bright red)
                        const ImU32 cp = IM_COL32(220, 150, 255, 255); // BPIO (bright purple)
                        auto forTokens = [&](auto emit) {
                            for (int i = 0; i < 5; ++i)
                                if (dev->pishock_enabled[i] && config_.pishock_shocker_ids[i] != 0) {
                                    char t[6]; std::snprintf(t, 6, "S%d", i); emit(t, cb);
                                }
                            for (int i = 0; i < 5; ++i)
                                if (dev->openshock_enabled[i] && !config_.openshock_device_ids[i].empty()) {
                                    char t[6]; std::snprintf(t, 6, "S%d", i); emit(t, cr);
                                }
                            for (int i = 0; i < 5; ++i)
                                if (dev->vibration_device_enabled[i] && config_.buttplug_device_indices[i] >= 0) {
                                    char t[6]; std::snprintf(t, 6, "V%d", i); emit(t, cp);
                                }
                        };
                        float tw = 0.0f;
                        forTokens([&](const char* t, ImU32){ tw += ImGui::CalcTextSize(t).x + 4.0f; });
                        if (tw > 0.0f) {
                            float sx = c.x - tw * 0.5f;
                            float sy = c.y - hot/2 - 16.0f;
                            if (sy < origin.y + 1.0f) sy = c.y + hot/2 + 2.0f; // top slot: draw below
                            forTokens([&](const char* t, ImU32 col){
                                drawOutlined(ImVec2(sx, sy), col, t);
                                sx += ImGui::CalcTextSize(t).x + 4.0f;
                            });
                        }
                    }
                }
            }
            // JawOpen (VRCFT) hotspot at head center. Separate from kSlots because
            // JawOpen is a scalar parameter, not a tracked device. Drop the JawOpen
            // sidebar item here to enable it; drop a shocker ID chip here to bind it.
            // Position sits over the head/muzzle (tune to the dashed-line effigy art).
            {
                const float jux = 0.460f, juy = 0.115f;
                ImVec2 c(origin.x + jux*imgW, origin.y + juy*imgH);
                ImGui::SetCursorScreenPos(ImVec2(c.x - hot/2, c.y - hot/2));
                ImGui::InvisibleButton("JawOpenSlot", ImVec2(hot, hot));
                bool hovered = ImGui::IsItemHovered();

                if (ImGui::BeginDragDropTarget()) {
                    if (ImGui::AcceptDragDropPayload("SPVR_JAW")) {
                        config_.jawopen_enabled = true;
                        LoadJawBindingsFromConfig();
                        SaveConfig();
                    }
                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("SPVR_SHOCKID")) {
                        ApplyIdBindingToJaw(static_cast<const char*>(p->Data));
                    }
                    ImGui::EndDragDropTarget();
                }
                if (ImGui::IsItemClicked()) { jaw_selected_ = true; selected_slot_role_ = DeviceRole::None; }

                bool on = config_.jawopen_enabled;
                ImU32 status_col = IM_COL32(130, 150, 170, 120); // disabled (dim)
                if (on) {
                    status_col = jaw_.exceeds_threshold ? IM_COL32(255, 70, 70, 255)
                               : jaw_.in_warning_zone  ? IM_COL32(255, 215, 60, 255)
                                                       : IM_COL32(70, 220, 100, 255);
                }
                ImU32 ring = hovered ? IM_COL32(255, 255, 255, 255) : status_col;
                dl->AddCircleFilled(c, hot/2 - 2, on ? IM_COL32(30, 60, 45, 150) : IM_COL32(20, 24, 32, 140));
                dl->AddCircle(c, hot/2, ring, 24, on ? 3.0f : 1.5f);
                if (jaw_selected_)
                    dl->AddCircle(c, hot/2 + 3, IM_COL32(80, 160, 255, 255), 24, 2.0f);
                drawOutlined(ImVec2(c.x + hot/2 + 3, c.y - 7),
                             on ? status_col : IM_COL32(190, 200, 220, 230), "Jaw");
            }

            // Top-left drop targets: bind/unbind a dragged ID across every
            // assigned cuff at once (in addition to dropping on a single slot).
            ImGui::SetCursorScreenPos(ImVec2(paneOrigin.x + 2.0f, paneOrigin.y + 2.0f));
            ImGui::BeginGroup();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.55f, 0.25f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.72f, 0.34f, 1.0f));
            ImGui::Button("All Cuffs");
            ImGui::PopStyleColor(2);
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("SPVR_SHOCKID"))
                    ApplyIdBindingToAllCuffs(static_cast<const char*>(p->Data), true);
                ImGui::EndDragDropTarget();
            }
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.20f, 0.20f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.72f, 0.28f, 0.28f, 1.0f));
            ImGui::Button("Remove From All Cuffs");
            ImGui::PopStyleColor(2);
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("SPVR_SHOCKID"))
                    ApplyIdBindingToAllCuffs(static_cast<const char*>(p->Data), false);
                ImGui::EndDragDropTarget();
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.88f, 0.95f, 1.0f));
            ImGui::TextUnformatted("(drag IDs into here");
            ImGui::TextUnformatted("or onto individual devices)");
            ImGui::PopStyleColor();
            ImGui::EndGroup();

            ImGui::SetCursorScreenPos(origin);
            ImGui::Dummy(box);

            // Click on empty effigy space (no slot/button under the cursor)
            // deselects the current slot and closes the config panel below.
            if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                selected_slot_role_ = DeviceRole::None;
                jaw_selected_ = false;
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // ---- RIGHT (neutral space): ID palette on top, device list below ----
        ImGui::BeginChild("RightPane", ImVec2(0, effigyH), false);
        ImGui::TextWrapped("Drag a device onto a body slot to assign it. Drag an ID chip onto a "
                           "body slot to bind a shocker/vibrator. Drag the JawOpen (VRCFT) item "
                           "onto the head to enable the jaw constraint. Click a device, slot, or "
                           "the head to configure it.");
        ImGui::SeparatorText("Available IDs (drag onto a body slot)");
        RenderShockerPalette();

        ImGui::SeparatorText("Devices");
        ImGui::BeginChild("DeviceList", ImVec2(0, 0), true);

        // JawOpen (VRCFT) virtual item: drag onto the head to enable; heat bar
        // shows the live jaw value. Not a tracked device, so it's listed first.
        {
            ImGui::PushID("jawopen_item");
            if (ImGui::Selectable("JawOpen (VRCFT)", jaw_selected_, 0,
                                  ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0))) {
                jaw_selected_ = true; selected_slot_role_ = DeviceRole::None;
            }
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                ImGui::SetDragDropPayload("SPVR_JAW", "JAW", 4);
                ImGui::TextUnformatted("Assign JawOpen to head");
                ImGui::EndDragDropSource();
            }
            ImGui::SameLine();
            float jh = jaw_.current;
            ImVec4 jhc(0.25f + 0.75f*jh, 0.30f, 0.95f*(1.0f-jh), 1.0f);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, jhc);
            ImGui::ProgressBar(jh, ImVec2(70, 0), jh > 0.6f ? "open" : (jh > 0.15f ? "ajar" : "closed"));
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (config_.jawopen_enabled)
                ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.55f, 1.0f), "-> Head");
            else
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "(disabled)");
            ImGui::PopID();
            ImGui::Separator();
        }

        if (device_positions_.empty()) {
            ImGui::TextDisabled("No devices detected (SteamVR not connected?).");
        }
        for (auto& d : device_positions_) {
            ImGui::PushID(d.serial.c_str());
            std::string row = d.device_name.empty() ? d.serial : d.device_name;

            // Name selectable: drag source to assign to a slot; click selects the
            // device's slot for configuration.
            bool is_sel = (d.role != DeviceRole::None && d.role == selected_slot_role_);
            if (ImGui::Selectable(row.c_str(), is_sel, 0, ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0))) {
                if (d.role != DeviceRole::None) selected_slot_role_ = d.role;
            }
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                ImGui::SetDragDropPayload("SPVR_DEVICE", d.serial.c_str(), d.serial.size() + 1);
                ImGui::Text("Assign %s", row.c_str());
                ImGui::EndDragDropSource();
            }
            ImGui::SameLine();
            float h = d.movement_heat;
            ImVec4 hc(0.25f + 0.75f*h, 0.30f, 0.95f*(1.0f-h), 1.0f);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, hc);
            ImGui::ProgressBar(h, ImVec2(70, 0), h > 0.5f ? "HOT" : (h > 0.12f ? "warm" : "idle"));
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (d.role == DeviceRole::None)
                ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1.0f), "(unassigned)");
            else
                ImGui::TextColored(ImVec4(0.45f,0.9f,0.55f,1.0f), "-> %s", RoleName(d.role));
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndChild();

        // ---- Per-slot configure panel ----
        if (jaw_selected_) {
            ImGui::Separator();
            RenderJawConfig();
        } else if (selected_slot_role_ != DeviceRole::None) {
            ImGui::Separator();
            RenderSlotConfig(selected_slot_role_);
        }
    }

    void UIManager::RenderSlotConfig(DeviceRole role) {
        ImGui::Text("Configure slot: %s", RoleName(role));
        ImGui::SameLine();
        if (ImGui::SmallButton("Close")) {
            selected_slot_role_ = DeviceRole::None;
            return;
        }
        std::string serial = SerialForRole(role);
        if (serial.empty()) {
            ImGui::TextDisabled("No device assigned. Drag one onto this slot.");
            return;
        }
        DevicePosition* dev = nullptr;
        for (auto& d : device_positions_) if (d.serial == serial) { dev = &d; break; }
        if (!dev) return;

        ImGui::Text("Assigned: %s", dev->device_name.empty() ? serial.c_str() : dev->device_name.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Unassign")) {
            AssignRoleToSerial(serial, DeviceRole::None);
            selected_slot_role_ = DeviceRole::None;
            return;
        }

        // Lock/unlock just this device.
        if (dev->locked) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
            if (ImGui::Button("Unlock This Device")) {
                LockDevicePosition(serial, false);
                if (config_.audio.enabled && config_.audio.unlock)
                    AudioManager::PlayUnlockSound(config_.audio.volume);
            }
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.5f, 1.0f, 1.0f));
            if (ImGui::Button("Lock This Device")) {
                LockDevicePosition(serial, true);
                if (config_.audio.enabled && config_.audio.lock)
                    AudioManager::PlayLockSound(config_.audio.volume);
            }
            ImGui::PopStyleColor();
        }

        bool inc = dev->include_in_locking;
        if (ImGui::Checkbox("Include in Lock All", &inc)) {
            dev->include_in_locking = inc;
            config_.device_settings[serial] = inc;
            SaveConfig();
        }

        // PiShock — the configured shocker slots (0-based to match the panel).
        ImGui::TextColored(ImVec4(0.45f, 0.65f, 1.0f, 1.0f), "PiShock:");
        bool any_ps = false;
        for (int i = 0; i < 5; ++i) {
            if (config_.pishock_shocker_ids[i] == 0) continue;
            any_ps = true;
            ImGui::SameLine();
            ImGui::PushID(1000 + i);
            bool on = dev->pishock_enabled[i];
            std::string lbl = std::to_string(i);
            if (ImGui::Checkbox(lbl.c_str(), &on)) {
                dev->pishock_enabled[i] = on;
                config_.device_pishock_ids[serial] = dev->pishock_enabled;
                SaveConfig();
            }
            ImGui::PopID();
        }
        if (!any_ps) { ImGui::SameLine(); ImGui::TextDisabled("(none configured)"); }

        // OpenShock — the configured device slots.
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "OpenShock:");
        bool any_os = false;
        for (int i = 0; i < 5; ++i) {
            if (config_.openshock_device_ids[i].empty()) continue;
            any_os = true;
            ImGui::SameLine();
            ImGui::PushID(1500 + i);
            bool on = dev->openshock_enabled[i];
            std::string lbl = std::to_string(i);
            if (ImGui::Checkbox(lbl.c_str(), &on)) {
                dev->openshock_enabled[i] = on;
                config_.device_openshock_ids[serial] = dev->openshock_enabled;
                SaveConfig();
            }
            ImGui::PopID();
        }
        if (!any_os) { ImGui::SameLine(); ImGui::TextDisabled("(none configured)"); }

        // Vibration — the 5 configured Buttplug device indices.
        ImGui::Text("Vibration (Buttplug / BPIO):");
        bool any_vibe = false;
        for (int i = 0; i < 5; ++i) {
            if (config_.buttplug_device_indices[i] < 0) continue;
            any_vibe = true;
            ImGui::SameLine();
            ImGui::PushID(2000 + i);
            bool on = dev->vibration_device_enabled[i];
            std::string lbl = std::to_string(i);
            if (ImGui::Checkbox(lbl.c_str(), &on)) {
                dev->vibration_device_enabled[i] = on;
                config_.device_vibration_ids[serial] = dev->vibration_device_enabled;
                SaveConfig();
            }
            ImGui::PopID();
        }
        if (!any_vibe) { ImGui::SameLine(); ImGui::TextDisabled("(none configured)"); }
    }

    // Config panel for the JawOpen (VRCFT) head hotspot. Mirrors RenderSlotConfig
    // but operates on the jaw_ state and the reserved kJawOpenSerial bindings.
    void UIManager::RenderJawConfig() {
        ImGui::Text("Configure: JawOpen (VRCFT)");
        ImGui::SameLine();
        if (ImGui::SmallButton("Close")) {
            jaw_selected_ = false;
            return;
        }

        bool enabled = config_.jawopen_enabled;
        if (ImGui::Checkbox("Enabled", &enabled)) {
            config_.jawopen_enabled = enabled;
            if (enabled) LoadJawBindingsFromConfig();
            SaveConfig();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Unassign")) {
            config_.jawopen_enabled = false;
            jaw_selected_ = false;
            SaveConfig();
            return;
        }

        // Live state readout.
        ImGui::Text("Value: %.2f", jaw_.current);
        ImGui::SameLine();
        if (config_.jawopen_enabled && jaw_.active) {
            ImGui::Text("| baseline %.2f, dev %.2f %s", jaw_.baseline, jaw_.deviation,
                        jaw_.exceeds_threshold ? "[DISOBEDIENCE]" :
                        jaw_.in_warning_zone ? "[WARNING]" : "[SAFE]");
        } else if (config_.jawopen_enabled && jaw_.in_grace) {
            ImGui::TextDisabled("| capturing baseline...");
        } else if (config_.jawopen_enabled) {
            ImGui::TextDisabled("| inactive (lock HMD)");
        }

        float warn = config_.jawopen_warning_margin;
        if (ImGui::SliderFloat("Warning margin", &warn, 0.01f, 0.5f, "%.2f"))
            config_.jawopen_warning_margin = warn;
        if (ImGui::IsItemDeactivatedAfterEdit()) SaveConfig();

        float diso = config_.jawopen_disobedience_margin;
        if (ImGui::SliderFloat("Disobedience margin", &diso, 0.02f, 0.8f, "%.2f"))
            config_.jawopen_disobedience_margin = diso;
        if (ImGui::IsItemDeactivatedAfterEdit()) SaveConfig();

        // Shocker/vibrator bindings (reserved serial). Mirrors RenderSlotConfig.
        ImGui::TextColored(ImVec4(0.45f, 0.65f, 1.0f, 1.0f), "PiShock:");
        bool any_ps = false;
        for (int i = 0; i < 5; ++i) {
            if (config_.pishock_shocker_ids[i] == 0) continue;
            any_ps = true; ImGui::SameLine(); ImGui::PushID(3000 + i);
            bool on = jaw_.pishock_enabled[i];
            if (ImGui::Checkbox(std::to_string(i).c_str(), &on)) {
                jaw_.pishock_enabled[i] = on;
                config_.device_pishock_ids[kJawOpenSerial] = jaw_.pishock_enabled;
                SaveConfig();
            }
            ImGui::PopID();
        }
        if (!any_ps) { ImGui::SameLine(); ImGui::TextDisabled("(none configured)"); }

        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "OpenShock:");
        bool any_os = false;
        for (int i = 0; i < 5; ++i) {
            if (config_.openshock_device_ids[i].empty()) continue;
            any_os = true; ImGui::SameLine(); ImGui::PushID(3500 + i);
            bool on = jaw_.openshock_enabled[i];
            if (ImGui::Checkbox(std::to_string(i).c_str(), &on)) {
                jaw_.openshock_enabled[i] = on;
                config_.device_openshock_ids[kJawOpenSerial] = jaw_.openshock_enabled;
                SaveConfig();
            }
            ImGui::PopID();
        }
        if (!any_os) { ImGui::SameLine(); ImGui::TextDisabled("(none configured)"); }

        ImGui::Text("Vibration (Buttplug / BPIO):");
        bool any_vibe = false;
        for (int i = 0; i < 5; ++i) {
            if (config_.buttplug_device_indices[i] < 0) continue;
            any_vibe = true; ImGui::SameLine(); ImGui::PushID(4000 + i);
            bool on = jaw_.vibration_device_enabled[i];
            if (ImGui::Checkbox(std::to_string(i).c_str(), &on)) {
                jaw_.vibration_device_enabled[i] = on;
                config_.device_vibration_ids[kJawOpenSerial] = jaw_.vibration_device_enabled;
                SaveConfig();
            }
            ImGui::PopID();
        }
        if (!any_vibe) { ImGui::SameLine(); ImGui::TextDisabled("(none configured)"); }
    }

    void UIManager::RenderDevicesTab() {
        ImGui::Text("Device Management");
        ImGui::Separator();

        if (ImGui::BeginTabBar("DevicesSubTabs")) {
            // Visual: the avatar-effigy assignment view; shown first as the default.
            if (ImGui::BeginTabItem("Visual")) {
                RenderVisualAssignment();
                ImGui::EndTabItem();
            }

            // Classic: the existing per-device table (kept as the reliable fallback).
            if (ImGui::BeginTabItem("Classic")) {
                RenderDeviceList();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }

    void UIManager::RenderBoundariesTab() {
        // This tab is no longer used, as boundary settings were moved to the Main tab.
        ImGui::Text("Boundary settings have been moved to the Main tab.");
    }

    void UIManager::RenderLockControls() {
        ImGui::PushID("LockSection");
        
        ImGui::Text("Position Lock Controls");
        ImGui::Separator();
        
        // Position threshold slider
        ImGui::Text("Position Threshold:");
        ImGuiHelpers::SliderFloatWithButtons("##PosThreshold", &position_threshold_, 0.01f, 0.5f, 0.01f, "%.2f m");
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
        
        if (ImGui::BeginTable("DevicesTable", 7, ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("Device Info");
            ImGui::TableSetupColumn("Role");
            ImGui::TableSetupColumn("Position & Rotation");
            ImGui::TableSetupColumn("Status");
            ImGui::TableSetupColumn("Lock Controls");
            ImGui::TableSetupColumn("Shock Devices");
            ImGui::TableSetupColumn("Vibration Devices");
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
                
                // Movement heat meter — for identifying which physical tracker is
                // which: wiggle it in SteamVR and its bar spikes red, then cools.
                {
                    float h = device.movement_heat;
                    ImVec4 heat_col(0.25f + 0.75f * h, 0.30f, 0.95f * (1.0f - h), 1.0f); // blue (idle) -> red (hot)
                    const char* lbl = (h > 0.5f) ? "MOVING" : (h > 0.12f) ? "moving" : "idle";
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, heat_col);
                    ImGui::ProgressBar(h, ImVec2(90, 0), lbl);
                    ImGui::PopStyleColor();
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
                
                // "Include in Lock All" checkbox: whether the Status tab's
                // Lock All (and the OSC global lock) affects this device. Clearer
                // than the old Will/Won't Lock toggle button.
                bool include = device.include_in_locking;
                if (ImGui::Checkbox("Include in Lock All", &include)) {
                    device.include_in_locking = include;
                    config_.device_settings[device.serial] = include;
                    if (StayPutVR::Logger::IsInitialized()) {
                        StayPutVR::Logger::Info("Device " + device.serial + " include_in_locking set to " +
                                                (include ? "true" : "false"));
                    }
                    SaveConfig();
                }

                // Individual lock/unlock button for this device.
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
                        if (config_.audio.enabled && config_.audio.unlock) {
                            AudioManager::PlayUnlockSound(config_.audio.volume);
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
                        if (config_.audio.enabled && config_.audio.lock) {
                            AudioManager::PlayLockSound(config_.audio.volume);
                        }
                    }
                    
                    ImGui::PopStyleColor(3);
                }
                
                ImGui::PopID();
                
                // Shock Devices column: PiShock and OpenShock, bound separately.
                ImGui::TableNextColumn();
                ImGui::PushID(("shockDevices" + device.serial).c_str());

                // Sync in-memory selection from config.
                {
                    auto ps_it = config_.device_pishock_ids.find(device.serial);
                    if (ps_it != config_.device_pishock_ids.end()) device.pishock_enabled = ps_it->second;
                    auto os_it = config_.device_openshock_ids.find(device.serial);
                    if (os_it != config_.device_openshock_ids.end()) device.openshock_enabled = os_it->second;
                }

                // One toggle row for a category (configured() gates which slots
                // show; on_color tints enabled buttons). 0-based labels.
                auto shock_row = [&](const char* name, std::array<bool, 5>& sel,
                                     std::unordered_map<std::string, std::array<bool, 5>>& store,
                                     ImVec4 on_color, auto configured) {
                    ImGui::TextUnformatted(name);
                    ImGui::SameLine();
                    bool any = false;
                    for (int i = 0; i < 5; ++i) {
                        if (!configured(i)) continue;
                        any = true;
                        ImGui::PushID(i);
                        if (sel[i]) {
                            ImGui::PushStyleColor(ImGuiCol_Button, on_color);
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                ImVec4(on_color.x + 0.1f, on_color.y + 0.1f, on_color.z + 0.1f, 1.0f));
                        } else {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
                        }
                        if (ImGui::Button(std::to_string(i).c_str(), ImVec2(25, 22))) {
                            sel[i] = !sel[i];
                            store[device.serial] = sel;
                            SaveConfig();
                        }
                        ImGui::PopStyleColor(2);
                        ImGui::PopID();
                        ImGui::SameLine();
                    }
                    if (!any) { ImGui::TextDisabled("(none)"); }
                    else      { ImGui::NewLine(); }
                };

                shock_row("PiShock  ", device.pishock_enabled, config_.device_pishock_ids,
                          ImVec4(0.20f, 0.45f, 0.85f, 1.0f),
                          [&](int i){ return config_.pishock_shocker_ids[i] != 0; });
                shock_row("OpenShock", device.openshock_enabled, config_.device_openshock_ids,
                          ImVec4(0.80f, 0.25f, 0.25f, 1.0f),
                          [&](int i){ return !config_.openshock_device_ids[i].empty(); });

                ImGui::PopID();
                
                // Vibration Devices column - for Buttplug integration
                ImGui::TableNextColumn();
                ImGui::PushID(("vibrationDevices" + device.serial).c_str());
                
                // Load device vibration settings from config
                auto vibration_it = config_.device_vibration_ids.find(device.serial);
                if (vibration_it != config_.device_vibration_ids.end()) {
                    device.vibration_device_enabled = vibration_it->second;
                }
                
                // Small buttons for vibration device selection (1-5)
                ImGui::Text("Vibration IDs:");
                ImGui::SameLine();
                ImGuiHelpers::HelpTooltip("Button 1 = Device Index 0, Button 2 = Device Index 1, etc.");
                for (int i = 0; i < 5; ++i) {
                    // Show button if Buttplug device is configured (-1 means not configured)
                    bool has_device = config_.buttplug_device_indices[i] >= 0;
                    if (has_device) {
                        ImGui::PushID(i);
                        
                        // Color the button based on whether it's enabled
                        if (device.vibration_device_enabled[i]) {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.8f, 1.0f));  // Purple for vibration
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.9f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.1f, 0.7f, 1.0f));
                        } else {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
                        }
                        
                        if (ImGui::Button(std::to_string(i + 1).c_str(), ImVec2(25, 25))) {
                            device.vibration_device_enabled[i] = !device.vibration_device_enabled[i];
                            config_.device_vibration_ids[device.serial] = device.vibration_device_enabled;
                            SaveConfig();
                        }
                        
                        ImGui::PopStyleColor(3);
                        ImGui::PopID();
                        
                        if (i < 4) ImGui::SameLine();
                    }
                }
                
                // All/None buttons
                ImGui::NewLine();
                if (ImGui::Button("All##Vibration", ImVec2(40, 20))) {
                    for (int i = 0; i < 5; ++i) {
                        bool has_device = config_.buttplug_device_indices[i] >= 0;  // -1 means not configured
                        if (has_device) {
                            device.vibration_device_enabled[i] = true;
                        }
                    }
                    config_.device_vibration_ids[device.serial] = device.vibration_device_enabled;
                    SaveConfig();
                }
                ImGui::SameLine();
                if (ImGui::Button("None##Vibration", ImVec2(40, 20))) {
                    for (int i = 0; i < 5; ++i) {
                        device.vibration_device_enabled[i] = false;
                    }
                    config_.device_vibration_ids[device.serial] = device.vibration_device_enabled;
                    SaveConfig();
                }
                
                ImGui::PopID();
            }
            
            ImGui::EndTable();
        }
    }

} // namespace StayPutVR
