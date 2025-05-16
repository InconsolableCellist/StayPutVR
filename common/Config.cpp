#include "Config.hpp"
#include <fstream>
#include "../thirdparty/inih/INIReader.h"
#include <filesystem>
#include <sstream>
#include <iostream>
#include "Logger.hpp"
#include <nlohmann/json.hpp>
#include "PathUtils.hpp"

Config::Config()
    : osc_address("127.0.0.1")
    , osc_port(9000)
    , osc_address_bounds("/stayputvr/bounds")
    , osc_address_warning("/stayputvr/warning")
    , osc_address_disable("/stayputvr/disable")
    , osc_enabled(false)
    , chaining_mode(false)
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
        if (StayPutVR::Logger::IsInitialized()) {
            StayPutVR::Logger::Error("Exception while creating default config: " + std::string(e.what()));
        }
        return false;
    }
}

bool Config::LoadFromFile(const std::string& filename) {
    try {
        // This function expects just the filename, not a full path
        std::string configPath = StayPutVR::GetAppDataPath() + "\\config\\" + filename;
        
        if (!std::filesystem::exists(configPath)) {
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Warning("Config file does not exist, creating default: " + configPath);
            }
            return CreateDefaultConfigFile(filename);
        }
        
        // Use INIReader to parse the config file
        INIReader reader(configPath);
        
        if (reader.ParseError() < 0) {
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Error("Failed to parse config file: " + configPath);
            }
            return false;
        }
        
        // Load OSC settings
        osc_address = reader.Get("osc", "address", osc_address);
        osc_port = reader.GetInteger("osc", "port", osc_port);
        osc_address_bounds = reader.Get("osc", "address_bounds", osc_address_bounds);
        osc_address_warning = reader.Get("osc", "address_warning", osc_address_warning);
        osc_address_disable = reader.Get("osc", "address_disable", osc_address_disable);
        osc_enabled = reader.GetBoolean("osc", "enabled", osc_enabled);
        chaining_mode = reader.GetBoolean("osc", "chaining_mode", chaining_mode);
        
        // Load boundary settings
        warning_threshold = reader.GetFloat("boundaries", "warning_threshold", warning_threshold);
        bounds_threshold = reader.GetFloat("boundaries", "bounds_threshold", bounds_threshold);
        disable_threshold = reader.GetFloat("boundaries", "disable_threshold", disable_threshold);
        
        // Load timer settings
        cooldown_enabled = reader.GetBoolean("timers", "cooldown_enabled", cooldown_enabled);
        cooldown_seconds = reader.GetFloat("timers", "cooldown_seconds", cooldown_seconds);
        countdown_enabled = reader.GetBoolean("timers", "countdown_enabled", countdown_enabled);
        countdown_seconds = reader.GetFloat("timers", "countdown_seconds", countdown_seconds);
        
        // Load notification settings
        audio_enabled = reader.GetBoolean("notifications", "audio_enabled", audio_enabled);
        audio_volume = reader.GetFloat("notifications", "audio_volume", audio_volume);
        warning_audio = reader.GetBoolean("notifications", "warning_audio", warning_audio);
        out_of_bounds_audio = reader.GetBoolean("notifications", "out_of_bounds_audio", out_of_bounds_audio);
        lock_audio = reader.GetBoolean("notifications", "lock_audio", lock_audio);
        unlock_audio = reader.GetBoolean("notifications", "unlock_audio", unlock_audio);
        haptic_enabled = reader.GetBoolean("notifications", "haptic_enabled", haptic_enabled);
        haptic_intensity = reader.GetFloat("notifications", "haptic_intensity", haptic_intensity);
        
        // Load application settings
        start_with_steamvr = reader.GetBoolean("application", "start_with_steamvr", start_with_steamvr);
        minimize_to_tray = reader.GetBoolean("application", "minimize_to_tray", minimize_to_tray);
        show_notifications = reader.GetBoolean("application", "show_notifications", show_notifications);
        
        // Load device names and settings from all sections that start with "device:"
        const std::set<std::string>& sections = reader.Sections();
        for (const auto& section : sections) {
            if (section.find("device:") == 0) {
                std::string serial = section.substr(7); // Skip "device:"
                
                // Load device name
                std::string name = reader.Get(section, "name", "Unknown Device");
                device_names[serial] = name;
                
                // Load include_in_locking setting
                bool include_in_locking = reader.GetBoolean(section, "include_in_locking", false);
                device_settings[serial] = include_in_locking;
                
                if (StayPutVR::Logger::IsInitialized()) {
                    StayPutVR::Logger::Info("Loaded device settings for: " + serial + 
                        " (name: " + name + ", include_in_locking: " + 
                        (include_in_locking ? "true" : "false") + ")");
                }
            }
        }
        
        if (StayPutVR::Logger::IsInitialized()) {
            StayPutVR::Logger::Info("Loaded config file: " + configPath);
        }
        
        return true;
    }
    catch (const std::exception& e) {
        if (StayPutVR::Logger::IsInitialized()) {
            StayPutVR::Logger::Error("Exception while loading config: " + std::string(e.what()));
        }
        return false;
    }
}

bool Config::SaveToFile(const std::string& filename) const {
    try {
        // This function expects just the filename, not a full path
        std::string configPath = StayPutVR::GetAppDataPath() + "\\config\\" + filename;
        
        // Make sure the directory exists
        std::filesystem::create_directories(StayPutVR::GetAppDataPath() + "\\config");
        
        std::ofstream out(configPath);
        if (!out.is_open()) {
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Error("Failed to open config file for writing: " + configPath);
            }
            return false;
        }
        
        // Write OSC settings
        out << "[osc]\n";
        out << "address = " << osc_address << "\n";
        out << "port = " << osc_port << "\n";
        out << "address_bounds = " << osc_address_bounds << "\n";
        out << "address_warning = " << osc_address_warning << "\n";
        out << "address_disable = " << osc_address_disable << "\n";
        out << "enabled = " << (osc_enabled ? "true" : "false") << "\n";
        out << "chaining_mode = " << (chaining_mode ? "true" : "false") << "\n\n";
        
        // Write boundary settings
        out << "[boundaries]\n";
        out << "warning_threshold = " << warning_threshold << "\n";
        out << "bounds_threshold = " << bounds_threshold << "\n";
        out << "disable_threshold = " << disable_threshold << "\n\n";
        
        // Write timer settings
        out << "[timers]\n";
        out << "cooldown_enabled = " << (cooldown_enabled ? "true" : "false") << "\n";
        out << "cooldown_seconds = " << cooldown_seconds << "\n";
        out << "countdown_enabled = " << (countdown_enabled ? "true" : "false") << "\n";
        out << "countdown_seconds = " << countdown_seconds << "\n\n";
        
        // Write notification settings
        out << "[notifications]\n";
        out << "audio_enabled = " << (audio_enabled ? "true" : "false") << "\n";
        out << "audio_volume = " << audio_volume << "\n";
        out << "warning_audio = " << (warning_audio ? "true" : "false") << "\n";
        out << "out_of_bounds_audio = " << (out_of_bounds_audio ? "true" : "false") << "\n";
        out << "lock_audio = " << (lock_audio ? "true" : "false") << "\n";
        out << "unlock_audio = " << (unlock_audio ? "true" : "false") << "\n";
        out << "haptic_enabled = " << (haptic_enabled ? "true" : "false") << "\n";
        out << "haptic_intensity = " << haptic_intensity << "\n\n";
        
        // Write application settings
        out << "[application]\n";
        out << "start_with_steamvr = " << (start_with_steamvr ? "true" : "false") << "\n";
        out << "minimize_to_tray = " << (minimize_to_tray ? "true" : "false") << "\n";
        out << "show_notifications = " << (show_notifications ? "true" : "false") << "\n\n";
        
        // Write device settings
        for (const auto& [serial, name] : device_names) {
            out << "[device:" << serial << "]\n";
            out << "name = " << name << "\n";
            
            // Get the include_in_locking setting for this device, default to false if not found
            bool include_in_locking = false;
            auto it = device_settings.find(serial);
            if (it != device_settings.end()) {
                include_in_locking = it->second;
            }
            
            out << "include_in_locking = " << (include_in_locking ? "true" : "false") << "\n\n";
        }
        
        out.close();
        
        if (StayPutVR::Logger::IsInitialized()) {
            StayPutVR::Logger::Info("Saved config file: " + configPath);
        }
        
        return true;
    }
    catch (const std::exception& e) {
        if (StayPutVR::Logger::IsInitialized()) {
            StayPutVR::Logger::Error("Exception while saving config: " + std::string(e.what()));
        }
        return false;
    }
} 