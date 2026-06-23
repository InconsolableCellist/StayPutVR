#pragma once

// Startup splash / "Welcome / About" overlay: logo, version, a welcome
// message, and live Patreon supporter names (fetched in the background with a
// bundled JSON fallback). Shown on every launch; an optional auto-close
// dismisses it after a brief delay. Reopenable from the Status tab.

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace StayPutVR {

    class Config;

    class SplashScreen {
    public:
        SplashScreen() = default;
        ~SplashScreen();

        // Directory containing logo.png / patreon_supporters.json.
        void SetAssetsPath(const std::string& path) { assets_path_ = path; }

        bool LoadLogo();          // GL texture upload; safe to call once.
        void LoadSupporters();    // spawns a background fetch (non-blocking).

        bool IsVisible() const { return visible_.load(); }
        void Hide() { visible_.store(false); }
        void Reshow();

        void SetAutoClose(bool enabled) { auto_close_ = enabled; }
        bool IsAutoClose() const { return auto_close_; }

        // Draws the overlay when visible. Persists the auto-close preference
        // to config when toggled here.
        void Render(Config& config);

    private:
        std::string assets_path_;

        unsigned int logo_tex_ = 0;
        int logo_w_ = 0;
        int logo_h_ = 0;

        std::atomic<bool> visible_{true};
        bool focus_next_frame_ = true;

        bool auto_close_ = false;
        bool auto_close_dirty_ = false;
        std::chrono::steady_clock::time_point shown_at_{};
        bool timer_running_ = false;
        static constexpr float AUTO_CLOSE_DELAY = 2.0f; // seconds

        // Supporter names, populated by the background fetch.
        std::mutex supporters_mutex_;
        std::atomic<bool> supporters_loading_{false};
        std::atomic<bool> supporters_done_{false};
        std::vector<std::string> vip_supporters_;
        std::vector<std::string> supporters_;

        void ParseSupportersJson(const std::string& json_str);
    };

} // namespace StayPutVR
