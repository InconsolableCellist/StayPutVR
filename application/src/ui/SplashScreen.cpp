#include "SplashScreen.hpp"

#include "../../../common/Config.hpp"
#include "../../../common/Logger.hpp"
#include "../../../common/HttpClient.hpp"
#include "../../../common/Version.hpp"

#include <glad/glad.h>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include "stb/stb_image.h" // impl lives in stb_image_impl.cpp

#include <algorithm>
#include <cfloat>
#include <fstream>
#include <map>
#include <thread>

namespace StayPutVR {

    namespace {
        constexpr const char* FOXIPSO_URL = "http://foxipso.com";
        // Reuses Foxipso's existing supporters worker (shared across apps).
        constexpr const char* SUPPORTERS_URL =
            "https://yipai-supporters.dan-a7b.workers.dev/supporters";
    }

    SplashScreen::~SplashScreen() {
        if (logo_tex_) {
            GLuint t = logo_tex_;
            glDeleteTextures(1, &t);
            logo_tex_ = 0;
        }
    }

    bool SplashScreen::LoadLogo() {
        if (logo_tex_ || assets_path_.empty()) return logo_tex_ != 0;
        std::string path = assets_path_ + "/logo.png";
        int w = 0, h = 0, ch = 0;
        unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &ch, 4);
        if (!pixels) {
            if (Logger::IsInitialized())
                Logger::Warning("SplashScreen: failed to load logo " + path);
            return false;
        }
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        logo_tex_ = tex;
        logo_w_ = w;
        logo_h_ = h;
        stbi_image_free(pixels);
        return true;
    }

    void SplashScreen::ParseSupportersJson(const std::string& json_str) {
        auto j = nlohmann::json::parse(json_str);
        std::lock_guard<std::mutex> lock(supporters_mutex_);
        if (j.contains("vip_supporters") && j["vip_supporters"].is_array()) {
            for (const auto& name : j["vip_supporters"])
                if (name.is_string()) vip_supporters_.push_back(name.get<std::string>());
        }
        if (j.contains("supporters") && j["supporters"].is_array()) {
            for (const auto& name : j["supporters"])
                if (name.is_string()) supporters_.push_back(name.get<std::string>());
        }
    }

    void SplashScreen::LoadSupporters() {
        if (supporters_loading_.exchange(true)) return; // only once
        std::string assets = assets_path_;
        std::thread([this, assets]() {
            // Mark the attempt finished on exit no matter which path we take.
            struct DoneGuard { std::atomic<bool>& f; ~DoneGuard() { f.store(true); } } guard{supporters_done_};
            // Try the live endpoint first (WinHTTP on Windows; the Linux dev
            // build stubs this, so it simply falls through to the local file).
            std::string response;
            std::map<std::string, std::string> headers;
            bool ok = HttpClient::SendHttpRequest(SUPPORTERS_URL, "GET", headers, "", response);
            if (ok && !response.empty()) {
                try {
                    ParseSupportersJson(response);
                    std::lock_guard<std::mutex> lock(supporters_mutex_);
                    if (!vip_supporters_.empty() || !supporters_.empty()) {
                        if (Logger::IsInitialized())
                            Logger::Info("SplashScreen: loaded supporters from remote");
                        return;
                    }
                } catch (const std::exception& e) {
                    if (Logger::IsInitialized())
                        Logger::Warning("SplashScreen: failed to parse remote supporters: " +
                                        std::string(e.what()));
                }
            }

            // Fallback: bundled JSON.
            if (assets.empty()) return;
            std::string path = assets + "/patreon_supporters.json";
            std::ifstream file(path);
            if (!file.is_open()) return;
            try {
                std::string contents((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
                ParseSupportersJson(contents);
            } catch (const std::exception& e) {
                if (Logger::IsInitialized())
                    Logger::Warning("SplashScreen: failed to parse " + path + ": " + e.what());
            }
        }).detach();
    }

    void SplashScreen::Reshow() {
        visible_.store(true);
        focus_next_frame_ = true;
        timer_running_ = false;
    }

    void SplashScreen::Render(Config& config) {
        if (!visible_.load()) return;

        ImGuiIO& io = ImGui::GetIO();
        ImVec2 viewport = io.DisplaySize;
        const float ui_scale = ImGui::GetFontSize() / 16.0f;
        const float max_w = 560.0f * ui_scale;
        const float max_h = 560.0f * ui_scale;
        ImVec2 win_size(std::min(viewport.x * 0.85f, max_w),
                        std::min(viewport.y * 0.9f, max_h));
        ImVec2 win_pos((viewport.x - win_size.x) * 0.5f,
                       (viewport.y - win_size.y) * 0.5f);

        // Dim the UI behind the splash (background draw list keeps the splash
        // itself un-dimmed).
        ImGui::GetBackgroundDrawList()->AddRectFilled(
            ImVec2(0, 0), viewport, IM_COL32(0, 0, 0, 160));

        ImGui::SetNextWindowPos(win_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(win_size, ImGuiCond_Always);
        if (focus_next_frame_) {
            ImGui::SetNextWindowFocus();
            focus_next_frame_ = false;
        }

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoScrollbar;

        ImGui::SetNextWindowBgAlpha(0.98f);
        if (!ImGui::Begin("##spvr_splash", nullptr, flags)) {
            ImGui::End();
            return;
        }

        // ---- Header: logo + title ----
        const float logo_display = 96.0f * ui_scale;
        if (logo_tex_) {
            ImGui::Image((ImTextureID)(intptr_t)logo_tex_,
                         ImVec2(logo_display, logo_display));
            ImGui::SameLine();
        }
        ImGui::BeginGroup();
        ImGui::TextColored(ImVec4(0.45f, 0.72f, 1.0f, 1.0f), "StayPutVR");
        ImGui::Text("v%s (%s)", STAYPUTVR_VERSION, STAYPUTVR_GIT_HASH);
        ImGui::TextDisabled("Lock yourself in place in VR");
        ImGui::EndGroup();

        ImGui::Separator();

        // ---- Welcome ----
        ImGui::TextWrapped(
            "Thanks for your support! This version adds lots of fixes! "
            "Questions, ideas, or bugs? Join my Discord or X/Twitter! -Foxipso");
        ImGui::Spacing();
        ImGui::TextLinkOpenURL("foxipso.com", FOXIPSO_URL);

        ImGui::Spacing();
        ImGui::Separator();

        // ---- Patreon supporters ----
        {
            std::lock_guard<std::mutex> lock(supporters_mutex_);
            if (vip_supporters_.empty() && supporters_.empty()) {
                if (!supporters_done_.load()) {
                    ImGui::TextDisabled("Loading supporters...");
                }
            } else {
                if (!vip_supporters_.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "VIP Supporters:");
                    std::string line;
                    for (size_t i = 0; i < vip_supporters_.size(); ++i) {
                        if (i > 0) line += ", ";
                        line += vip_supporters_[i];
                    }
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
                    ImGui::TextWrapped("%s", line.c_str());
                    ImGui::PopStyleColor();
                }
                if (!supporters_.empty()) {
                    ImGui::TextColored(ImVec4(0.45f, 0.72f, 1.0f, 1.0f), "Supporters:");
                    std::string line;
                    for (size_t i = 0; i < supporters_.size(); ++i) {
                        if (i > 0) line += ", ";
                        line += supporters_[i];
                    }
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
                    ImGui::TextWrapped("%s", line.c_str());
                    ImGui::PopStyleColor();
                }
                ImGui::Spacing();
                ImGui::TextLinkOpenURL("Support on Patreon", FOXIPSO_URL);
            }
        }

        // ---- Footer (pinned to bottom) ----
        const float line_h = ImGui::GetTextLineHeightWithSpacing();
        const float footer_h = ImGui::GetStyle().ItemSpacing.y * 2.0f + line_h * 2.0f + 8.0f;
        float footer_y = ImGui::GetWindowHeight() - footer_h - ImGui::GetStyle().WindowPadding.y;
        if (footer_y > ImGui::GetCursorPosY()) {
            ImGui::SetCursorPosY(footer_y);
        }
        ImGui::Separator();

        bool ac = auto_close_;
        if (ImGui::Checkbox("Auto-close", &ac)) {
            auto_close_ = ac;
            auto_close_dirty_ = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Automatically close this screen after a few seconds");
        }

        const float btn_w = 120.0f * ui_scale;
        ImGui::SameLine();
        const float avail = ImGui::GetContentRegionAvail().x;
        if (avail > btn_w) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - btn_w));
        }
        if (ImGui::Button("Continue", ImVec2(btn_w, 0))) {
            visible_.store(false);
        }

        if (auto_close_dirty_) {
            config.splash_auto_close = auto_close_;
            auto_close_dirty_ = false;
        }

        // Auto-close timer: counts down from when the splash became visible.
        if (auto_close_) {
            if (!timer_running_) {
                shown_at_ = std::chrono::steady_clock::now();
                timer_running_ = true;
            }
            float secs = std::chrono::duration<float>(
                std::chrono::steady_clock::now() - shown_at_).count();
            float remaining = AUTO_CLOSE_DELAY - secs;
            if (remaining <= 0.0f) {
                visible_.store(false);
            } else {
                ImGui::SameLine();
                ImGui::TextDisabled("(closing in %ds)", static_cast<int>(remaining) + 1);
            }
        } else {
            timer_running_ = false;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::TextUnformatted("(C) Foxipso 2026 -");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextLinkOpenURL("foxipso.com", FOXIPSO_URL);

        ImGui::End();
    }

} // namespace StayPutVR
