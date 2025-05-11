#pragma once

#include <string>
#include <unordered_map>
#include <vector>

class Config {
public:
    Config();
    ~Config() = default;

    bool LoadFromFile(const std::string& filename);
    bool SaveToFile(const std::string& filename) const;
    bool CreateDefaultConfigFile(const std::string& filename);

    // OSC Settings
    std::string osc_address;
    int osc_port;
    std::string osc_address_bounds;
    std::string osc_address_warning;
    std::string osc_address_disable;
    bool osc_enabled;
    bool chaining_mode;

    // Boundary Settings
    float warning_threshold;  // Warning zone distance in meters
    float bounds_threshold;   // Out of bounds distance in meters
    float disable_threshold;  // Disable distance in meters

    // Timer Settings
    bool cooldown_enabled;
    float cooldown_seconds;
    bool countdown_enabled;
    float countdown_seconds;

    // Notification Settings
    bool audio_enabled;
    float audio_volume;
    bool warning_audio;
    bool out_of_bounds_audio;
    bool lock_audio;
    bool unlock_audio;
    bool haptic_enabled;
    float haptic_intensity;

    // Application Settings
    bool start_with_steamvr;
    bool minimize_to_tray;
    bool show_notifications;

    // Device settings maps
    std::unordered_map<std::string, std::string> device_names; // serial -> name
    std::unordered_map<std::string, bool> device_settings; // serial -> include_in_locking
}; 