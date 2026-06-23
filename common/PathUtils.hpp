#pragma once

#include <string>
#include <iostream>
#include <filesystem>

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

    // Directory containing the running executable (NOT the current working
    // directory, which depends on how the app was launched). Used to locate
    // resources bundled next to the binary in a build/dev tree.
    inline std::string GetExecutableDir() {
#ifdef _WIN32
        wchar_t buf[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (n == 0 || n == MAX_PATH) return ".";
        std::wstring w(buf, n);
        size_t slash = w.find_last_of(L"\\/");
        std::wstring dir = (slash == std::wstring::npos) ? L"." : w.substr(0, slash);
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string result(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, &result[0], size_needed, nullptr, nullptr);
        if (!result.empty() && result.back() == '\0') result.pop_back();
        return result;
#else
        char buf[4096];
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n <= 0) return ".";
        buf[n] = '\0';
        std::string p(buf);
        size_t slash = p.find_last_of('/');
        return (slash == std::string::npos) ? "." : p.substr(0, slash);
#endif
    }

    // Resolve the resources directory (logo.png, whats_new.md, effigy.png,
    // DroidSans.ttf, *.wav, ...). Search order, first hit wins:
    //   1. <exe dir>/resources  - dev/build runs and any portable layout
    //   2. AppData/StayPutVR/resources - the installed copy
    //   3. ./resources          - last-resort CWD fallback
    // Returns option 2 even if missing so callers log a consistent path.
    inline std::string GetResourcesPath() {
        std::error_code ec;
        std::string exeRes = GetExecutableDir() + "/resources";
        if (std::filesystem::exists(exeRes, ec)) return exeRes;
        std::string appRes = GetAppDataPath() + "/resources";
        if (std::filesystem::exists(appRes, ec)) return appRes;
        return appRes;
    }

} // namespace StayPutVR
