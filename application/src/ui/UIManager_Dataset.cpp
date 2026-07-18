// Dataset tab: raw tracker-pose capture for training datasets.
//
// The capture philosophy is deliberately "record everything, label nothing":
// tracking errors, drift, AFK periods, and left-on-overnight sessions are kept
// in the stream — they are training signal (a model can only learn what AFK
// looks like from data that contains AFK), and filtering at capture time is
// irreversible. The only capture-time control is Pause, as a privacy
// affordance; paused intervals are recorded in the session manifest.

#include "UIManager.hpp"
#include <imgui.h>
#include <cinttypes>
#include <cstdio>
#include <filesystem>
#include "../../common/Logger.hpp"
#include "../../common/PathUtils.hpp"
#ifdef _WIN32
#include <shellapi.h> // ShellExecuteA
#else
#include <cstdlib>    // std::system (xdg-open)
#endif

namespace fs = std::filesystem;

namespace StayPutVR {

    namespace {
        void OpenFolder(const std::string& dir) {
            std::error_code ec;
            fs::create_directories(dir, ec);
#ifdef _WIN32
            ShellExecuteA(NULL, "open", dir.c_str(), NULL, NULL, SW_SHOWDEFAULT);
#else
            (void)std::system(("xdg-open '" + dir + "' >/dev/null 2>&1 &").c_str());
#endif
        }

        std::string FormatDuration(double seconds) {
            if (seconds < 0.0) seconds = 0.0;
            int total = static_cast<int>(seconds);
            int h = total / 3600, m = (total % 3600) / 60, s = total % 60;
            char buf[32];
            snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
            return buf;
        }

        std::string FormatBytes(uint64_t bytes) {
            char buf[32];
            if (bytes >= 1024ull * 1024 * 1024) {
                snprintf(buf, sizeof(buf), "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
            } else if (bytes >= 1024ull * 1024) {
                snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
            } else {
                snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
            }
            return buf;
        }
    }

    std::string UIManager::DatasetBaseDir() const {
        if (!config_.dataset_dir.empty()) {
            return config_.dataset_dir;
        }
        return DatasetRecorder::DefaultBaseDir();
    }

    void UIManager::RenderDatasetTab() {
        if (!dataset_recorder_) {
            ImGui::TextDisabled("Dataset recorder not initialized.");
            return;
        }
        DatasetRecorder& rec = *dataset_recorder_;

        // ----- Capture status + controls ---------------------------------
        ImGui::SeparatorText("Capture");

        const bool recording = rec.IsRecording();
        const bool paused = rec.IsPaused();

        if (recording && !paused) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "\xe2\x97\x8f RECORDING");
        } else if (recording && paused) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "\xe2\x8f\xb8 PAUSED");
        } else {
            ImGui::TextDisabled("\xe2\x97\x8b Idle");
        }

        if (recording) {
            double now_wall = std::chrono::duration<double>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            double total = now_wall - rec.SessionStartWall();
            double paused_s = rec.PausedSecondsTotal();
            ImGui::SameLine();
            ImGui::Text("  %s  (active %s)", FormatDuration(total).c_str(),
                        FormatDuration(total - paused_s).c_str());

            ImGui::Text("Frames: %" PRIu64 "   Samples: %" PRIu64 "   Written: %s",
                        rec.FramesWritten(), rec.SamplesWritten(),
                        FormatBytes(rec.BytesWritten()).c_str());
            if (rec.FramesDropped() > 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                   "Dropped %" PRIu64 " frames (disk not keeping up?)",
                                   rec.FramesDropped());
            }
            ImGui::TextDisabled("Session: %s", rec.SessionDir().c_str());
        }

        // Feed status: recording without a source produces an empty file —
        // make that state obvious.
        const bool driver_connected = device_manager_ && device_manager_->IsConnected();
        const bool simulating = device_manager_ && device_manager_->IsSimulating();
        if (driver_connected) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Driver: connected (~90 Hz feed)");
        } else if (simulating) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Feed: SIMULATED devices (dev)");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                               "Driver: not connected — nothing is being captured");
        }

        ImGui::Spacing();

        if (!recording) {
            if (ImGui::Button("Start Recording", ImVec2(160, 0))) {
                const char* source = simulating ? "simulator" : "steamvr-driver";
                if (!rec.StartSession(DatasetBaseDir(), source)) {
                    Logger::Error("UIManager: failed to start dataset session");
                }
            }
        } else {
            if (ImGui::Button("Stop Recording", ImVec2(160, 0))) {
                rec.StopSession();
                dataset_sessions_dirty_ = true;
            }
            ImGui::SameLine();
            if (paused) {
                if (ImGui::Button("Resume", ImVec2(120, 0))) {
                    rec.SetPaused(false);
                }
            } else {
                if (ImGui::Button("Pause", ImVec2(120, 0))) {
                    rec.SetPaused(true);
                }
            }
        }

        bool auto_record = config_.dataset_auto_record;
        if (ImGui::Checkbox("Start recording automatically on launch", &auto_record)) {
            config_.dataset_auto_record = auto_record;
            SaveConfig();
        }

        // Dev tool: synthetic 90 Hz device feed to exercise the capture path
        // without SteamVR (also how the Linux dev build tests recording).
        bool simulate = simulating;
        if (ImGui::Checkbox("Simulate device feed (dev)", &simulate)) {
            if (device_manager_) {
                if (simulate) device_manager_->StartSimulation();
                else device_manager_->StopSimulation();
            }
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Records your trackers' positions in realtime and saves them as datasets.");

        // ----- Dataset folder + sessions table ----------------------------
        ImGui::Spacing();
        ImGui::SeparatorText("Datasets");

        const std::string base_dir = DatasetBaseDir();
        ImGui::TextDisabled("Folder: %s", base_dir.c_str());
        if (ImGui::Button("Open Datasets Folder")) {
            OpenFolder(base_dir);
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
            dataset_sessions_dirty_ = true;
        }

        if (dataset_sessions_dirty_) {
            dataset_sessions_ = DatasetRecorder::ScanSessions(base_dir);
            dataset_sessions_dirty_ = false;
        }

        if (dataset_sessions_.empty()) {
            ImGui::TextDisabled("No recorded sessions yet.");
        } else {
            const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_SizingStretchProp |
                                          ImGuiTableFlags_ScrollY;
            const float table_height = ImGui::GetTextLineHeightWithSpacing() *
                                       (std::min<size_t>(dataset_sessions_.size(), 12) + 2.0f);
            if (ImGui::BeginTable("##dataset_sessions", 8, flags, ImVec2(0, table_height))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Session", ImGuiTableColumnFlags_WidthStretch, 2.0f);
                ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("Paused", ImGuiTableColumnFlags_WidthStretch, 0.8f);
                ImGui::TableSetupColumn("Frames", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 0.9f);
                ImGui::TableSetupColumn("Devices", ImGuiTableColumnFlags_WidthStretch, 0.7f);
                ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthStretch, 1.2f);
                ImGui::TableHeadersRow();

                for (const auto& s : dataset_sessions_) {
                    ImGui::TableNextRow();
                    ImGui::PushID(s.path.c_str());

                    const bool is_active = recording && s.path == rec.SessionDir();

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(s.dir_name.c_str());
                    if (is_active) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "(recording)");
                    } else if (!s.clean_shutdown) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "(unclean)");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Session did not shut down cleanly (crash or kill).\n"
                                              "Pose data is intact; counters may lag by ~15 s.");
                        }
                    }

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(FormatDuration(s.duration_total).c_str());
                    ImGui::TableNextColumn();
                    if (s.duration_paused > 0.5) {
                        ImGui::TextUnformatted(FormatDuration(s.duration_paused).c_str());
                    } else {
                        ImGui::TextDisabled("-");
                    }
                    ImGui::TableNextColumn();
                    ImGui::Text("%" PRIu64, s.frames);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(FormatBytes(s.bytes).c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", s.device_count);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(s.source.c_str());

                    ImGui::TableNextColumn();
                    if (ImGui::SmallButton("Open")) {
                        OpenFolder(s.path);
                    }
                    ImGui::SameLine();
                    ImGui::BeginDisabled(is_active);
                    if (ImGui::SmallButton("Delete")) {
                        dataset_delete_target_ = s.path;
                    }
                    ImGui::EndDisabled();

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }

        // ----- Delete confirmation ----------------------------------------
        if (!dataset_delete_target_.empty() && !ImGui::IsPopupOpen("Delete dataset?")) {
            ImGui::OpenPopup("Delete dataset?");
        }
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Delete dataset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Permanently delete this session and all its data?");
            ImGui::Spacing();
            ImGui::TextUnformatted(dataset_delete_target_.c_str());
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
            if (ImGui::Button("Delete", ImVec2(120, 0))) {
                std::error_code ec;
                fs::remove_all(dataset_delete_target_, ec);
                if (ec) {
                    Logger::Error("UIManager: failed to delete dataset " +
                                  dataset_delete_target_ + ": " + ec.message());
                }
                dataset_delete_target_.clear();
                dataset_sessions_dirty_ = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                dataset_delete_target_.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
}
