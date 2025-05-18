#include "Config.hpp"
#include <fstream>
#include "../thirdparty/inih/INIReader.h"
#include <filesystem>
#include <sstream>
#include <iostream>
#include "Logger.hpp"
#include <nlohmann/json.hpp>
#include "PathUtils.hpp"

namespace StayPutVR {

Config::Config()
    : log_level("WARNING")
    , osc_address("127.0.0.1")
    , osc_port(9000)
    , osc_address_bounds("/stayputvr/bounds")
    , osc_address_warning("/stayputvr/warning")
    , osc_address_disable("/stayputvr/disable")
    , osc_enabled(false)
    , chaining_mode(false)
    , pishock_enabled(false)
    , pishock_group(0)
    , pishock_warning_beep(false)
    , pishock_warning_shock(false)
    , pishock_warning_vibrate(false)
    , pishock_warning_intensity(0.25f)
    , pishock_warning_duration(0.25f)
    , pishock_disobedience_beep(false)
    , pishock_disobedience_shock(false)
    , pishock_disobedience_vibrate(false)
    , pishock_disobedience_intensity(0.5f)
    , pishock_disobedience_duration(0.5f)
    , warning_threshold(0.1f)
    , bounds_threshold(0.2f)
    , disable_threshold(0.5f)
    , cooldown_enabled(false)
    , cooldown_seconds(5.0f)
    , countdown_enabled(false)
    , countdown_seconds(3.0f)
    , audio_enabled(true)
    , audio_volume(0.8f)
    , warning_audio(true)
    , out_of_bounds_audio(true)
    , lock_audio(true)
    , unlock_audio(true)
    , haptic_enabled(true)
    , haptic_intensity(0.5f)
    , start_with_steamvr(true)
    , minimize_to_tray(false)
    , show_notifications(true)
{
}

bool Config::CreateDefaultConfigFile(const std::string& filename) {
    try {
        // Create the default config using the current default values
        return SaveToFile(filename);
    }
    catch (const std::exception& e) {
        if (Logger::IsInitialized()) {
            Logger::Error("Exception while creating default config: " + std::string(e.what()));
        }
        return false;
    }
}

bool Config::LoadFromFile(const std::string& filename) {
    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to open config file: " + filename);
            }
            return false;
        }

        nlohmann::json j;
        file >> j;

        // OSC settings
        osc_enabled = j.value("osc_enabled", false);
        osc_address = j.value("osc_address", "127.0.0.1");
        osc_port = j.value("osc_port", 9000);
        chaining_mode = j.value("chaining_mode", false);

        // PiShock settings
        pishock_enabled = j.value("pishock_enabled", false);
        pishock_group = j.value("pishock_group", 0);
        
        // Warning Zone PiShock Settings
        pishock_warning_beep = j.value("pishock_warning_beep", false);
        pishock_warning_shock = j.value("pishock_warning_shock", false);
        pishock_warning_vibrate = j.value("pishock_warning_vibrate", false);
        pishock_warning_intensity = j.value("pishock_warning_intensity", 0.25f);
        pishock_warning_duration = j.value("pishock_warning_duration", 0.25f);
        
        // Disobedience (Out of Bounds) PiShock Settings
        pishock_disobedience_beep = j.value("pishock_disobedience_beep", false);
        pishock_disobedience_shock = j.value("pishock_disobedience_shock", false);
        pishock_disobedience_vibrate = j.value("pishock_disobedience_vibrate", false);
        pishock_disobedience_intensity = j.value("pishock_disobedience_intensity", 0.5f);
        pishock_disobedience_duration = j.value("pishock_disobedience_duration", 0.5f);

        // Load logging settings
        log_level = j.value("log_level", "WARNING");
        
        // Load boundary settings
        warning_threshold = j.value("warning_threshold", 0.1f);
        bounds_threshold = j.value("bounds_threshold", 0.2f);
        disable_threshold = j.value("disable_threshold", 0.5f);
        
        // Load timer settings
        cooldown_enabled = j.value("cooldown_enabled", false);
        cooldown_seconds = j.value("cooldown_seconds", 5.0f);
        countdown_enabled = j.value("countdown_enabled", false);
        countdown_seconds = j.value("countdown_seconds", 3.0f);
        
        // Load notification settings
        audio_enabled = j.value("audio_enabled", true);
        audio_volume = j.value("audio_volume", 0.8f);
        warning_audio = j.value("warning_audio", true);
        out_of_bounds_audio = j.value("out_of_bounds_audio", true);
        lock_audio = j.value("lock_audio", true);
        unlock_audio = j.value("unlock_audio", true);
        haptic_enabled = j.value("haptic_enabled", true);
        haptic_intensity = j.value("haptic_intensity", 0.5f);
        
        // Load application settings
        start_with_steamvr = j.value("start_with_steamvr", true);
        minimize_to_tray = j.value("minimize_to_tray", false);
        show_notifications = j.value("show_notifications", true);
        
        // Load device names and settings from all sections that start with "device:"
        const nlohmann::json& devices = j.value("devices", nlohmann::json::array());
        for (const auto& device : devices) {
            std::string serial = device.value("serial", "Unknown Device");
            std::string name = device.value("name", "Unknown Device");
            bool include_in_locking = device.value("include_in_locking", false);
            device_names[serial] = name;
            device_settings[serial] = include_in_locking;
            
            if (Logger::IsInitialized()) {
                Logger::Info("Loaded device settings for: " + serial + 
                    " (name: " + name + ", include_in_locking: " + 
                    (include_in_locking ? "true" : "false") + ")");
            }
        }
        
        if (Logger::IsInitialized()) {
            Logger::Info("Loaded config file: " + filename);
        }
        return true;
    }
    catch (const std::exception& e) {
        if (Logger::IsInitialized()) {
            Logger::Error("Error loading config: " + std::string(e.what()));
        }
        return false;
    }
}

bool Config::SaveToFile(const std::string& filename) const {
    try {
        nlohmann::json j;

        // OSC settings
        j["osc_enabled"] = osc_enabled;
        j["osc_address"] = osc_address;
        j["osc_port"] = osc_port;
        j["chaining_mode"] = chaining_mode;

        // PiShock settings
        j["pishock_enabled"] = pishock_enabled;
        j["pishock_group"] = pishock_group;
        
        // Warning Zone PiShock Settings
        j["pishock_warning_beep"] = pishock_warning_beep;
        j["pishock_warning_shock"] = pishock_warning_shock;
        j["pishock_warning_vibrate"] = pishock_warning_vibrate;
        j["pishock_warning_intensity"] = pishock_warning_intensity;
        j["pishock_warning_duration"] = pishock_warning_duration;
        
        // Disobedience (Out of Bounds) PiShock Settings
        j["pishock_disobedience_beep"] = pishock_disobedience_beep;
        j["pishock_disobedience_shock"] = pishock_disobedience_shock;
        j["pishock_disobedience_vibrate"] = pishock_disobedience_vibrate;
        j["pishock_disobedience_intensity"] = pishock_disobedience_intensity;
        j["pishock_disobedience_duration"] = pishock_disobedience_duration;

        // Load logging settings
        j["log_level"] = log_level;
        
        // Load boundary settings
        j["warning_threshold"] = warning_threshold;
        j["bounds_threshold"] = bounds_threshold;
        j["disable_threshold"] = disable_threshold;
        
        // Load timer settings
        j["cooldown_enabled"] = cooldown_enabled;
        j["cooldown_seconds"] = cooldown_seconds;
        j["countdown_enabled"] = countdown_enabled;
        j["countdown_seconds"] = countdown_seconds;
        
        // Load notification settings
        j["audio_enabled"] = audio_enabled;
        j["audio_volume"] = audio_volume;
        j["warning_audio"] = warning_audio;
        j["out_of_bounds_audio"] = out_of_bounds_audio;
        j["lock_audio"] = lock_audio;
        j["unlock_audio"] = unlock_audio;
        j["haptic_enabled"] = haptic_enabled;
        j["haptic_intensity"] = haptic_intensity;
        
        // Load application settings
        j["start_with_steamvr"] = start_with_steamvr;
        j["minimize_to_tray"] = minimize_to_tray;
        j["show_notifications"] = show_notifications;
        
        // Load device names and settings from all sections that start with "device:"
        nlohmann::json devices = nlohmann::json::array();
        for (const auto& [serial, name] : device_names) {
            nlohmann::json device;
            device["serial"] = serial;
            device["name"] = name;
            device["include_in_locking"] = device_settings.find(serial) != device_settings.end() && device_settings.at(serial);
            devices.push_back(device);
        }
        j["devices"] = devices;
        
        std::ofstream file(filename);
        if (!file.is_open()) {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to open config file for writing: " + filename);
            }
            return false;
        }

        file << j.dump(4);
        
        if (Logger::IsInitialized()) {
            Logger::Info("Saved config file: " + filename);
        }
        return true;
    }
    catch (const std::exception& e) {
        if (Logger::IsInitialized()) {
            Logger::Error("Error saving config: " + std::string(e.what()));
        }
        return false;
    }
}

} // namespace StayPutVR 