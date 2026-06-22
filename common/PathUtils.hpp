#pragma once

#include <string>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <combaseapi.h>
#else
#include <cstdlib>
#include <pwd.h>
#include <unistd.h>
#endif

namespace StayPutVR {

#ifdef _WIN32
    // Gets the path to the user's AppData\Roaming\StayPutVR directory
    inline std::string GetAppDataPath() {
        std::string result;
        PWSTR path = nullptr;

        // Get the AppData\Roaming folder path
        HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path);

        if (SUCCEEDED(hr)) {
            // Convert wide string to narrow string
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
            std::string appdata_path(size_needed, 0);
            WideCharToMultiByte(CP_UTF8, 0, path, -1, &appdata_path[0], size_needed, nullptr, nullptr);

            // Clean up the string (remove null terminator if present)
            if (!appdata_path.empty() && appdata_path[appdata_path.size() - 1] == '\0') {
                appdata_path.resize(appdata_path.size() - 1);
            }

            // Build the final path with StayPutVR subfolder
            result = appdata_path + "\\StayPutVR";

            // Free the allocated memory for path
            CoTaskMemFree(path);
        } else {
            // Fallback if we can't get the AppData path
            result = ".\\StayPutVR";
            std::cerr << "Failed to get AppData path (HRESULT: " << std::hex << hr << "), using current directory as fallback" << std::endl;
        }

        return result;
    }
#else
    // Linux: follow the XDG Base Directory spec, falling back to ~/.local/share.
    // Used by the development/OSC-simulation build (no SteamVR driver on Linux).
    inline std::string GetAppDataPath() {
        const char* xdg_data = std::getenv("XDG_DATA_HOME");
        if (xdg_data && xdg_data[0] != '\0') {
            return std::string(xdg_data) + "/StayPutVR";
        }

        const char* home = std::getenv("HOME");
        if (!home || home[0] == '\0') {
            if (struct passwd* pw = getpwuid(getuid())) {
                home = pw->pw_dir;
            }
        }

        if (home && home[0] != '\0') {
            return std::string(home) + "/.local/share/StayPutVR";
        }

        // Last-resort fallback to the current directory.
        return "./StayPutVR";
    }
#endif

} // namespace StayPutVR
