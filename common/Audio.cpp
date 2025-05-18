#include "Audio.hpp"
#include <filesystem>
#include <algorithm> // For std::max and std::min
#include "PathUtils.hpp"
#include "Logger.hpp"

namespace StayPutVR {

    std::string AudioManager::resources_path_ = "";
    bool AudioManager::initialized_ = false;

    void AudioManager::Initialize() {
        if (initialized_) return;

        // Set the resources path to be relative to the executable
        resources_path_ = GetAppDataPath() + "\\resources";
        
        // Check if resources directory exists
        if (!std::filesystem::exists(resources_path_)) {
            try {
                // Try to create the directory if it doesn't exist
                std::filesystem::create_directories(resources_path_);
                if (Logger::IsInitialized()) {
                    Logger::Info("AudioManager: Created resources directory at " + resources_path_);
                }
            }
            catch (const std::exception& e) {
                if (Logger::IsInitialized()) {
                    Logger::Error("AudioManager: Failed to create resources directory: " + std::string(e.what()));
                }
                // Fall back to current directory
                resources_path_ = ".\\resources";
            }
        }

        initialized_ = true;
        
        if (Logger::IsInitialized()) {
            Logger::Info("AudioManager: Initialized with resources path: " + resources_path_);
        }
    }

    void AudioManager::Shutdown() {
        initialized_ = false;
    }

    bool AudioManager::PlaySound(const std::string& filename, float volume) {
        if (!initialized_) {
            Initialize();
        }

        std::string fullPath = resources_path_ + "\\" + filename;
        
        // Check if file exists
        if (!std::filesystem::exists(fullPath)) {
            if (Logger::IsInitialized()) {
                Logger::Error("AudioManager: Sound file not found: " + fullPath);
            }
            return false;
        }

        // Convert to wide string for Windows API
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, fullPath.c_str(), -1, NULL, 0);
        std::wstring wFullPath(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, fullPath.c_str(), -1, &wFullPath[0], size_needed);
        
        // Calculate volume level (0-1000)
        int volumeLevel = static_cast<int>(volume * 1000);
        // Clamp volume to valid range
        volumeLevel = (std::max)(0, (std::min)(volumeLevel, 1000));

        // Apply volume setting using waveOutSetVolume
        WORD leftVolume = static_cast<WORD>(volumeLevel * 65.535f); // Convert 0-1000 to 0-65535
        WORD rightVolume = leftVolume;
        DWORD dwVolume = MAKELONG(leftVolume, rightVolume);
        waveOutSetVolume(NULL, dwVolume);
        
        // Play sound asynchronously
        DWORD flags = SND_FILENAME | SND_ASYNC | SND_NODEFAULT;
        if (::PlaySoundW(wFullPath.c_str(), NULL, flags)) {
            // Successfully started playing the sound
            if (Logger::IsInitialized()) {
                Logger::Debug("AudioManager: Playing sound: " + filename + " with volume level: " + std::to_string(volumeLevel));
            }
            return true;
        } else {
            DWORD error = GetLastError();
            if (Logger::IsInitialized()) {
                Logger::Error("AudioManager: Failed to play sound: " + filename + ", Error: " + std::to_string(error));
            }
            return false;
        }
    }

    bool AudioManager::PlayWarningSound(float volume) {
        return PlaySound("warning.wav", volume);
    }

    bool AudioManager::PlayOutOfBoundsSound(float volume) {
        // Just use warning.wav as fallback - disobedience.wav is explicitly 
        // checked in the UIManager when needed
        if (StayPutVR::Logger::IsInitialized()) {
            StayPutVR::Logger::Warning("AudioManager: Using warning.wav for out of bounds");
        }
        return PlaySound("warning.wav", volume);
    }

    bool AudioManager::PlayLockSound(float volume) {
        // If lock.wav doesn't exist, use Windows default sound
        std::string filePath = resources_path_ + "\\lock.wav";
        if (std::filesystem::exists(filePath)) {
            return PlaySound("lock.wav", volume);
        } else {
            if (Logger::IsInitialized()) {
                Logger::Warning("AudioManager: lock.wav not found, using system sound");
            }
            
            // Apply volume to system sound
            WORD leftVolume = static_cast<WORD>(volume * 65535.0f);
            WORD rightVolume = leftVolume;
            DWORD dwVolume = MAKELONG(leftVolume, rightVolume);
            waveOutSetVolume(NULL, dwVolume);
            
            // Use Windows system sound (asterisk) as fallback
            DWORD flags = SND_ALIAS | SND_ASYNC | SND_NODEFAULT;
            return ::PlaySoundW(L"SystemAsterisk", NULL, flags);
        }
    }

    bool AudioManager::PlayUnlockSound(float volume) {
        // If unlock.wav doesn't exist, use Windows default sound
        std::string filePath = resources_path_ + "\\unlock.wav";
        if (std::filesystem::exists(filePath)) {
            return PlaySound("unlock.wav", volume);
        } else {
            if (Logger::IsInitialized()) {
                Logger::Warning("AudioManager: unlock.wav not found, using system sound");
            }
            
            // Apply volume to system sound
            WORD leftVolume = static_cast<WORD>(volume * 65535.0f);
            WORD rightVolume = leftVolume;
            DWORD dwVolume = MAKELONG(leftVolume, rightVolume);
            waveOutSetVolume(NULL, dwVolume);
            
            // Use Windows system sound (exclamation) as fallback
            DWORD flags = SND_ALIAS | SND_ASYNC | SND_NODEFAULT;
            return ::PlaySoundW(L"SystemExclamation", NULL, flags);
        }
    }

    void AudioManager::StopSound() {
        // Use the PlaySound Windows API with the SND_PURGE flag to stop current sound
        ::PlaySoundW(NULL, NULL, SND_PURGE);
        
        if (Logger::IsInitialized()) {
            Logger::Debug("AudioManager: Stopped all playing sounds");
        }
    }

} // namespace StayPutVR 