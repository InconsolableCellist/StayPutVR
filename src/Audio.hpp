#pragma once

#include <string>
#include <Windows.h>
#include <mmsystem.h>
#include "PathUtils.hpp"
#include "Logger.hpp"

#pragma comment(lib, "winmm.lib")

namespace StayPutVR {

    class AudioManager {
    public:
        static void Initialize();
        static void Shutdown();
        
        // Play audio files with different flags
        static bool PlayWarningSound(float volume = 1.0f);
        static bool PlayOutOfBoundsSound(float volume = 1.0f);
        static bool PlayLockSound(float volume = 1.0f);
        static bool PlayUnlockSound(float volume = 1.0f);
        
    private:
        static bool PlaySound(const std::string& filename, float volume = 1.0f);
        static std::string resources_path_;
        static bool initialized_;
    };

} // namespace StayPutVR 