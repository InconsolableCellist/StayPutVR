#pragma once

#include <string>
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <combaseapi.h>
#include <iostream>

namespace StayPutVR {

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

} // namespace StayPutVR 