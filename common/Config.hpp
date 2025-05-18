#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

namespace StayPutVR {

class Config {
public:
    Config();
    ~Config() = default;

    // These methods expect just the filename (e.g., "config.ini"), not a full path.
    // The path will be constructed internally using GetAppDataPath() + "\\config\\" + filename
    bool LoadFromFile(const std::string& filename);
    bool SaveToFile(const std::string& filename) const;
    bool CreateDefaultConfigFile(const std::string& filename);

    // Logging Settings
    std::string log_level;

    // OSC Settings
    bool osc_enabled = false;
    std::string osc_address = "127.0.0.1";
    int osc_send_port = 9000;
    int osc_receive_port = 9005;
    bool chaining_mode = false;
    std::string osc_address_bounds;
    std::string osc_address_warning;
    std::string osc_address_disable;
    
    // PiShock Settings via VRCOSC
    bool pishock_enabled = false;
    int pishock_group = 0;
    bool pishock_user_agreement = false;
    
    // PiShock Direct API Settings
    std::string pishock_api_key;
    std::string pishock_username;
    std::string pishock_share_code;
    
    // Warning Zone PiShock Settings
    bool pishock_warning_beep = false;
    bool pishock_warning_shock = false;
    bool pishock_warning_vibrate = false;
    float pishock_warning_intensity = 0.25f;
    float pishock_warning_duration = 0.25f;
    
    // Disobedience (Out of Bounds) PiShock Settings
    bool pishock_disobedience_beep = false;
    bool pishock_disobedience_shock = false;
    bool pishock_disobedience_vibrate = false;
    float pishock_disobedience_intensity = 0.5f;
    float pishock_disobedience_duration = 0.5f;

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

} // namespace StayPutVR 