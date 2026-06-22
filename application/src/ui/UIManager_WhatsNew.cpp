// What's New window + splash overlay glue.
//
// The What's New notes are loaded from resources/whats_new.md (updated each
// release). The window auto-shows once per version after the splash is
// dismissed, and is reopenable from the Status tab. Dismissing it records the
// current version in config.whats_new_seen_version so it stays gone until the
// next release.

#include "UIManager.hpp"
#include "ImGuiHelpers.hpp"
#include "../../../common/Config.hpp"
#include "../../../common/Logger.hpp"
#include "../../../common/Version.hpp"

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace StayPutVR {

    void UIManager::OpenWhatsNew() {
        show_whats_new_ = true;
    }

    // Draws the splash and What's New overlays on top of the main window.
    void UIManager::RenderSplashOverlay() {
        bool splash_visible = splash_ && splash_->IsVisible();

        // Auto-show What's New once per launch, only after the splash is gone
        // and only for a version the user hasn't dismissed yet.
        if (!whats_new_checked_ && !splash_visible) {
            whats_new_checked_ = true;
            if (config_.whats_new_seen_version != STAYPUTVR_VERSION) {
                show_whats_new_ = true;
            }
        }

        RenderWhatsNew();

        if (splash_visible) {
            splash_->Render(config_);
        }
    }

    void UIManager::RenderWhatsNew() {
        if (!show_whats_new_) return;

        // Lazy-load the notes; fall back to a changelog pointer if the asset
        // is missing (e.g. dev runs without the resource copied).
        if (!whats_new_loaded_) {
            whats_new_loaded_ = true;
            std::filesystem::path p = std::filesystem::path(assets_path_) / "whats_new.md";
            std::ifstream f(p);
            if (f) {
                std::ostringstream ss;
                ss << f.rdbuf();
                whats_new_text_ = ss.str();
            } else {
                whats_new_text_ = "# What's New\n- See the README's Version History "
                                  "for this release's notes.\n";
            }
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                   viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        float w = std::min(620.0f, viewport->WorkSize.x * 0.9f);
        float h = std::min(560.0f, viewport->WorkSize.y * 0.9f);
        ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Appearing);

        bool open = true;
        char title[80];
        snprintf(title, sizeof(title), "What's New in StayPutVR v%s###whatsnew",
                 STAYPUTVR_VERSION);
        if (ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse)) {
            float footer_h = ImGui::GetFrameHeightWithSpacing() + 4.0f;
            ImGui::BeginChild("##whatsnew_body", ImVec2(0, -footer_h));

            // Minimal markdown: "# "/"## " headers, "- " bullets, blank lines
            // as spacing, everything else wrapped text.
            std::istringstream lines(whats_new_text_);
            std::string line;
            const ImVec4 header_col(0.45f, 0.72f, 1.0f, 1.0f);
            while (std::getline(lines, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.rfind("# ", 0) == 0) {
                    ImGui::TextColored(header_col, "%s", line.c_str() + 2);
                    ImGui::Separator();
                } else if (line.rfind("## ", 0) == 0) {
                    ImGui::Spacing();
                    ImGui::TextColored(header_col, "%s", line.c_str() + 3);
                } else if (line.rfind("- ", 0) == 0) {
                    ImGui::Bullet();
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", line.c_str() + 2);
                } else if (line.empty()) {
                    ImGui::Spacing();
                } else {
                    size_t start = line.find_first_not_of(' ');
                    ImGui::TextWrapped("%s", line.c_str() +
                                       (start == std::string::npos ? 0 : start));
                }
            }
            ImGui::EndChild();

            ImGui::Separator();
            if (ImGui::Button("Close", ImVec2(120, 0))) {
                open = false;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Reopen any time from the Status tab.");
        }
        ImGui::End();

        if (!open) {
            show_whats_new_ = false;
            config_.whats_new_seen_version = STAYPUTVR_VERSION;
            SaveConfig();
        }
    }

} // namespace StayPutVR
