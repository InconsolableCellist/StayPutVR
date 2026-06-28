#include "UIManager.hpp"
#include "ImGuiHelpers.hpp"
#include <iostream>
#include <string>
#include <format>
#include <algorithm>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include "stb/stb_image.h" // effigy PNG decode (impl is in stb_image_impl.cpp)
#include <nlohmann/json.hpp>
#include "../../common/Logger.hpp"
#include "../../common/PathUtils.hpp"
#include "../../common/Audio.hpp"
#ifdef _WIN32
#include <shellapi.h> // For ShellExecuteA
#else
#include <cstdlib> // For std::system (xdg-open)
#endif
#include <thread> // For std::this_thread::sleep_for
#include "../../../common/OSCManager.hpp"
#include "../../common/HttpClient.hpp"

// Windows-specific includes for icon handling
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include "../resource.h"
#endif

using json = nlohmann::json;

extern std::atomic<bool> g_running;

namespace StayPutVR {

    UIManager::UIManager() : window_(nullptr), imgui_context_(nullptr), running_ptr_(&g_running) {
        // Initialize config_dir_ with AppData path
        config_dir_ = GetAppDataPath() + "\\config";
        // Initialize config_file_ with just the filename, not the full path
        config_file_ = "config.ini";
        // Increase window height to prevent cutting off UI elements
        window_height_ = 850;
        
        // Create device manager instance
        device_manager_ = new DeviceManager();
        
        // Initialize timestamps
        last_sound_time_ = std::chrono::steady_clock::now();
        last_osc_toggle_time_ = std::chrono::steady_clock::now();
    }

    UIManager::~UIManager() {
        Shutdown();
        
        ShutdownTwitchManager();
        ShutdownPiShockManager();
        ShutdownOpenShockManager();
        ShutdownButtplugManager();
        
        if (device_manager_) {
            delete device_manager_;
            device_manager_ = nullptr;
        }
    }

    bool UIManager::Initialize() {
        glfwSetErrorCallback(UIManager::GlfwErrorCallback);
        
        if (!glfwInit()) {
            std::cerr << "Failed to initialize GLFW" << std::endl;
            return false;
        }
        
        // GL 3.0 + GLSL 130
        const char* glsl_version = "#version 130";
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        
        // Create window with graphics context
        window_ = glfwCreateWindow(window_width_, window_height_, window_title_.c_str(), nullptr, nullptr);
        if (window_ == nullptr) {
            std::cerr << "Failed to create GLFW window" << std::endl;
            return false;
        }
        
#ifdef _WIN32
        // Set window icon
        HWND hwnd = glfwGetWin32Window(window_);
        if (hwnd) {
            HICON hIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_ICON1));
            if (hIcon) {
                SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
                SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
            }
        }
#endif
        
        glfwMakeContextCurrent(window_);
        glfwSwapInterval(1); // Enable vsync
        
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cerr << "Failed to initialize GLAD" << std::endl;
            return false;
        }
        
        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        imgui_context_ = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls

        ApplyTheme();

        // Load a vector font (nicer than the stock 13px bitmap). DroidSans.ttf is
        // vendored under thirdparty/imgui/misc/fonts and shipped in resources/.
        // ImGui 1.92 builds the atlas texture lazily; just AddFontFromFileTTF.
        {
            std::string fontPath = GetResourcesPath() + "/DroidSans.ttf";
            if (std::filesystem::exists(fontPath)) {
                static const ImWchar ranges[] = {
                    0x0020, 0x00FF, // Basic Latin + Latin-1 Supplement
                    0x2000, 0x206F, // General Punctuation (dashes, ellipsis, quotes)
                    0,
                };
                ImFontConfig cfg;
                cfg.PixelSnapH = true;
                if (!io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 16.0f, &cfg, ranges)) {
                    Logger::Warning("UIManager: failed to load DroidSans.ttf; using default font");
                }
            } else {
                Logger::Warning("UIManager: DroidSans.ttf not found in resources; using default font");
            }
        }

        // Setup Platform/Renderer backends
        ImGui_ImplGlfw_InitForOpenGL(window_, true);
        ImGui_ImplOpenGL3_Init(glsl_version);
        
        // Set window close callback
        glfwSetWindowCloseCallback(window_, [](GLFWwindow* window) {
            g_running = false;
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Info("Window close callback triggered");
            }
        });
        
        // Create all necessary directories
        std::string appDataPath = GetAppDataPath();
        std::string logPath = appDataPath + "\\logs";
        std::string configPath = appDataPath + "\\config";
        std::string resourcesPath = appDataPath + "\\resources";
        
        try {
            std::filesystem::create_directories(logPath);
            std::filesystem::create_directories(configPath);
            std::filesystem::create_directories(resourcesPath);
            
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Info("Created directories if needed: " + logPath);
            }
        } catch (const std::exception& e) {
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Error("Failed to create directories: " + std::string(e.what()));
            }
        }
        
        // Create config directories
        std::filesystem::create_directories(config_dir_);
        std::filesystem::create_directories(GetAppDataPath() + "\\config");
        
        // Initialize audio system
        AudioManager::Initialize();
        
        // Load configuration
        LoadConfig();

        // Resolve the resources directory (logo, whats_new.md, supporters json),
        // shared with the font/effigy lookup (exe dir, then AppData).
        assets_path_ = GetResourcesPath();

        // Startup splash / Welcome overlay. Shows on every launch; auto-close
        // is opt-in and persisted in config.
        splash_ = std::make_unique<SplashScreen>();
        splash_->SetAssetsPath(assets_path_);
        splash_->LoadLogo();
        splash_->LoadSupporters();
        splash_->SetAutoClose(config_.splash_auto_close);

        // Automatically connect to OSC if it was previously enabled
        if (config_.osc_enabled) {
            if (Logger::IsInitialized()) {
                Logger::Info("UIManager: OSC was previously enabled, connecting automatically");
            }
            
            // Add a small delay to ensure all components are properly initialized
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Try to initialize OSC. With OSCQuery on, bind the receive socket
            // to an ephemeral port so it never conflicts with another OSC app.
            bool osc_use_query = config_.osc_query_enabled;
            bool osc_init_result = OSCManager::GetInstance().Initialize(config_.osc_address, config_.osc_send_port,
                                                                        config_.osc_receive_port, osc_use_query);

            if (osc_init_result) {
                osc_enabled_ = true;
                
                // Configure OSC paths
                OSCManager::GetInstance().SetConfig(config_);
                
                // Register every inbound-OSC callback in one place (shared with
                // HandleOSCConnection via VerifyOSCCallbacks) so a startup
                // auto-connect and a manual reconnect register the identical set.
                RegisterOSCCallbacks();

                // Start OSCQuery (mDNS) so VRChat can discover our ephemeral
                // receive port and we can discover VRChat's OSC port.
                if (osc_use_query) {
                    StartOSCQuery();
                }

                if (Logger::IsInitialized()) {
                    Logger::Info("UIManager: OSC auto-connection successful, callbacks registered");
                }
            } else {
                if (Logger::IsInitialized()) {
                    Logger::Error("UIManager: OSC auto-connection failed, will need manual activation");
                }
                osc_enabled_ = false;
                config_.osc_enabled = false;
                SaveConfig();
            }
        }
        
        // Initialize device manager - but continue even if it fails
        if (device_manager_) {
            if (!device_manager_->Initialize()) {
                if (StayPutVR::Logger::IsInitialized()) {
                    StayPutVR::Logger::Warning("Failed to connect to driver IPC server - continuing without device connection");
                }
                // Don't return false here, just continue with the UI
            }
        }
        
        InitializeTwitchManager();
        InitializePiShockManager();
        InitializePiShockWebSocketManager();
        InitializeOpenShockManager();
        InitializeButtplugManager();

        // Create UI panels
        pishock_panel_ = std::make_unique<PiShockPanel>(
            config_, pishock_manager_, pishock_ws_manager_,
            [this]() { SaveConfig(); });
        openshock_panel_ = std::make_unique<OpenShockPanel>(
            config_, openshock_manager_,
            [this]() { SaveConfig(); });
        buttplug_panel_ = std::make_unique<ButtplugPanel>(
            config_, buttplug_manager_,
            [this]() { SaveConfig(); });

        // Microphone enforced-mute. Create the capture manager; start it only if the
        // feature is enabled+agreed. Seed the collar valid-mode mask and mic bindings.
        microphone_manager_ = std::make_unique<MicrophoneManager>();
        if (config_.mic_enabled && config_.mic_user_agreement) {
            microphone_manager_->SetDevice(config_.mic_device_id);
            microphone_manager_->Start();
        }
        LoadMicBindingsFromConfig();
        RecomputeCollarValidMask();

        return true;
    }

    void UIManager::Update() {
        // Poll and handle events
        glfwPollEvents();
        
        // Check if window should close
        if (glfwWindowShouldClose(window_)) {
            *running_ptr_ = false;
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Info("Window close button pressed, shutting down application");
            }
            glfwSetWindowShouldClose(window_, GLFW_FALSE); // Reset the flag
        }
        
        // Update countdown timer if active
        if (countdown_active_) {
            auto current_time = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - countdown_last_beep_).count();
            
            // Check if countdown has finished (3 seconds)
            if (elapsed >= countdown_remaining_) {
                countdown_active_ = false;
                if (StayPutVR::Logger::IsInitialized()) {
                    StayPutVR::Logger::Info("Countdown finished, activating lock");
                }
                ActivateGlobalLockInternal(true);
            }
        }
        
        if (twitch_manager_) {
            twitch_manager_->Update();
        }
        
        if (pishock_ws_manager_) {
            pishock_ws_manager_->Update();
        }
        
        if (buttplug_manager_) {
            buttplug_manager_->Update();
        }
        
        ProcessTwitchUnlockTimer();
        
        ProcessGlobalOutOfBoundsTimer();
        ProcessBiteTimer();
        ProcessAvatarResyncTimer();
        
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        
        if (device_manager_) {
            device_manager_->Update();
            
            const auto& devices = device_manager_->GetDevices();
            
            UpdateDevicePositions(devices);
        }
    }

    void UIManager::Render() {
        RenderMainWindow();
        
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window_, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        
        glfwSwapBuffers(window_);
    }

    void UIManager::Shutdown() {
        if (StayPutVR::Logger::IsInitialized()) {
            StayPutVR::Logger::Info("UIManager shutting down");
        }

        // Save configuration before shutting down
        SaveConfig();

        // Stop the microphone capture thread before tearing down.
        if (microphone_manager_) {
            microphone_manager_->Stop();
        }

        // Release the effigy GL texture (Devices > Visual).
        if (effigy_tex_ != 0) {
            GLuint t = effigy_tex_;
            glDeleteTextures(1, &t);
            effigy_tex_ = 0;
        }

        // Release the VRCFT / Unified Expressions logo textures (Integrations > VRCFT).
        if (vrcft_logo_tex_ != 0) {
            GLuint t = vrcft_logo_tex_;
            glDeleteTextures(1, &t);
            vrcft_logo_tex_ = 0;
        }
        if (ue_logo_tex_ != 0) {
            GLuint t = ue_logo_tex_;
            glDeleteTextures(1, &t);
            ue_logo_tex_ = 0;
        }
        if (bitetech_logo_tex_ != 0) {
            GLuint t = bitetech_logo_tex_;
            glDeleteTextures(1, &t);
            bitetech_logo_tex_ = 0;
        }

        // Stop OSCQuery (mDNS) threads so they release their sockets cleanly.
        StopOSCQuery();

        // Shutdown managers
        ShutdownTwitchManager();
        ShutdownPiShockManager();
        ShutdownOpenShockManager();
        ShutdownButtplugManager();
        
        // Give managers time to properly clean up (especially WebSocket connections)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // Shutdown device manager and IPC connection
        if (device_manager_) {
            Logger::Info("UIManager: Shutting down device manager");
            try {
                device_manager_->Shutdown();
                // Give some time for the IPC connection to properly close
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            catch (const std::exception& e) {
                Logger::Error("Exception when shutting down device manager: " + std::string(e.what()));
            }
        }
        
        AudioManager::Shutdown();
        
        if (window_ != nullptr) {
            // Cleanup
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext(imgui_context_);
            
            glfwDestroyWindow(window_);
            glfwTerminate();
            
            window_ = nullptr;
            imgui_context_ = nullptr;
        }
    }

    
    
    
    
    
    
    
    

    void UIManager::RenderMainWindow() {
        // Apply the user's font-size multiplier (Settings > Display).
        ImGui::GetIO().FontGlobalScale = config_.ui_font_scale;

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        
        ImGui::Begin("StayPutVR Control Panel", nullptr, 
            ImGuiWindowFlags_NoDecoration | 
            ImGuiWindowFlags_NoMove | 
            ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysVerticalScrollbar);

        RenderTabBar();

        // Persistent config-health warning sits directly under the tabs so it is
        // visible from any tab whenever settings can't be read or saved.
        RenderConfigHealthWarning();

        switch (current_tab_) {
            case TabType::MAIN:
                RenderMainTab();
                break;
            case TabType::DEVICES:
                RenderDevicesTab();
                break;
            case TabType::BOUNDARIES:
                RenderBoundariesTab();
                break;
            case TabType::NOTIFICATIONS:
                RenderNotificationsTab();
                break;
            case TabType::TIMERS:
                RenderTimersTab();
                break;
            case TabType::OSC:
                RenderOSCTab();
                break;
            case TabType::PISHOCK:
                RenderPiShockTab();
                break;
            case TabType::OPENSHOCK:
                RenderOpenShockTab();
                break;
            case TabType::BUTTPLUG:
                RenderButtplugTab();
                break;
            case TabType::TWITCH:
                RenderTwitchTab();
                break;
            case TabType::INTEGRATIONS:
                RenderIntegrationsTab();
                break;
            case TabType::SETTINGS:
                RenderSettingsTab();
                break;
        }

        ImGui::End();

        // Splash + What's New overlays draw on top of the main window.
        RenderSplashOverlay();
    }

    void UIManager::RenderConfigHealthWarning() {
        const bool load_problem =
            config_load_status_ == ConfigStatus::AccessDenied ||
            config_load_status_ == ConfigStatus::Corrupt ||
            config_load_status_ == ConfigStatus::OtherError;
        const bool problem = load_problem || config_save_failing_;

        // The folder we want the user to inspect / fix permissions on.
        const std::string configDir = GetAppDataPath() + "\\config";

        auto open_config_folder = []() {
            std::string dir = GetAppDataPath() + "/config";
#ifdef _WIN32
            ShellExecuteA(NULL, "open", dir.c_str(), NULL, NULL, SW_SHOWDEFAULT);
#else
            (void)std::system(("xdg-open '" + dir + "' >/dev/null 2>&1 &").c_str());
#endif
        };

        if (problem) {
            // Headline tuned to the dominant problem: a blocked save ("settings
            // won't stick") is what users actually feel, so it wins the banner.
            const char* headline =
                config_save_failing_ ? "Your settings are NOT being saved."
                : config_load_status_ == ConfigStatus::Corrupt ? "Your saved settings could not be read (file was corrupt)."
                : config_load_status_ == ConfigStatus::AccessDenied ? "Your saved settings could not be read (access denied)."
                : "There is a problem with your settings file.";

            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.35f, 0.10f, 0.10f, 1.0f));
            ImGui::BeginChild("##config_health", ImVec2(0, 0),
                              ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "\xe2\x9a\xa0  %s", headline);
            ImGui::TextWrapped(
                "Likely a permissions problem with the config folder -- often left over from "
                "running StayPutVR (or its installer) as Administrator, a read-only file, or "
                "antivirus / Controlled Folder Access blocking it.");
            ImGui::TextWrapped("Folder: %s", configDir.c_str());
            if (!config_health_detail_.empty()) {
                ImGui::TextWrapped("Details: %s", config_health_detail_.c_str());
            }
            if (ImGui::Button("Open Config Folder##health")) open_config_folder();
            ImGui::SameLine();
            if (ImGui::Button("How to fix##health")) {
                config_health_modal_pending_ = true;
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::Separator();
        }

        // One-time (per problem) explanatory modal with remediation steps.
        if (config_health_modal_pending_) {
            ImGui::OpenPopup("Settings File Problem");
            config_health_modal_pending_ = false;
        }
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Settings File Problem", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped(
                "StayPutVR can't %s your settings file.",
                config_save_failing_ ? "save" : "read");
            ImGui::Spacing();
            ImGui::TextWrapped("File location:");
            ImGui::TextWrapped("    %s", config_health_path_.empty()
                               ? (configDir + "\\config.ini").c_str()
                               : config_health_path_.c_str());
            if (!config_health_detail_.empty()) {
                ImGui::Spacing();
                ImGui::TextWrapped("System error: %s", config_health_detail_.c_str());
            }
            ImGui::Spacing();
            ImGui::SeparatorText("How to fix");
            ImGui::TextWrapped(
                "1. Close StayPutVR and SteamVR.\n"
                "2. Do NOT run StayPutVR 'as Administrator' -- if you have been, that is the\n"
                "   most likely cause. Launch it normally instead.\n"
                "3. Open the config folder (button below). If the config.ini or its folder is\n"
                "   owned by another account or marked read-only, right-click > Properties and\n"
                "   either clear Read-only or take ownership (Security tab).\n"
                "4. If you use antivirus or Windows 'Controlled Folder Access', allow StayPutVR\n"
                "   to write to AppData.\n"
                "5. As a last resort, delete config.ini and reconfigure -- a fresh one will be\n"
                "   written with correct permissions.");
            ImGui::Spacing();
            if (ImGui::Button("Open Config Folder##modal")) open_config_folder();
            ImGui::SameLine();
            if (ImGui::Button("Close##modal")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }




    void UIManager::RenderTabBar() {
        if (ImGui::BeginTabBar("##Tabs", ImGuiTabBarFlags_None)) {
            if (ImGui::BeginTabItem("Status")) {
                current_tab_ = TabType::MAIN;
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Devices")) {
                current_tab_ = TabType::DEVICES;
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Integrations")) {
                current_tab_ = TabType::INTEGRATIONS;
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Settings")) {
                current_tab_ = TabType::SETTINGS;
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    
    void UIManager::ApplyTheme() {
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();

        // Geometry — rounded frames/buttons + roomier spacing.
        style.WindowRounding    = 6.0f;
        style.ChildRounding     = 5.0f;
        style.FrameRounding     = 4.0f;
        style.PopupRounding     = 5.0f;
        style.GrabRounding      = 4.0f;
        style.TabRounding       = 4.0f;
        style.ScrollbarRounding = 8.0f;
        style.WindowPadding     = ImVec2(10.0f, 8.0f);
        style.FramePadding      = ImVec2(7.0f, 4.0f);
        style.ItemSpacing       = ImVec2(8.0f, 5.0f);
        style.ScrollbarSize     = 13.0f;
        style.WindowBorderSize  = 0.0f;
        style.ChildBorderSize   = 1.0f;

        // Soft-blue dark palette (ported from the companion app).
        const ImVec4 bg         = ImVec4(0.080f, 0.080f, 0.100f, 1.0f);
        const ImVec4 bg_panel   = ImVec4(0.110f, 0.110f, 0.135f, 1.0f);
        const ImVec4 bg_raised  = ImVec4(0.150f, 0.150f, 0.180f, 1.0f);
        const ImVec4 bg_hover   = ImVec4(0.210f, 0.210f, 0.250f, 1.0f);
        const ImVec4 bg_active  = ImVec4(0.260f, 0.260f, 0.310f, 1.0f);
        const ImVec4 accent     = ImVec4(0.60f, 0.80f, 1.00f, 1.0f);
        const ImVec4 accent_dim = ImVec4(0.26f, 0.42f, 0.58f, 1.0f);
        const ImVec4 accent_fnt = ImVec4(0.20f, 0.30f, 0.42f, 1.0f);
        auto mix = [](const ImVec4& a, const ImVec4& b, float k) {
            return ImVec4(a.x+(b.x-a.x)*k, a.y+(b.y-a.y)*k, a.z+(b.z-a.z)*k, a.w+(b.w-a.w)*k);
        };
        const ImVec4 accent_mid = mix(accent_dim, accent, 0.4f);

        ImVec4* c = style.Colors;
        c[ImGuiCol_WindowBg]          = bg;
        c[ImGuiCol_ChildBg]           = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_PopupBg]           = ImVec4(bg_panel.x, bg_panel.y, bg_panel.z, 0.98f);
        c[ImGuiCol_FrameBg]           = bg_raised;
        c[ImGuiCol_FrameBgHovered]    = bg_hover;
        c[ImGuiCol_FrameBgActive]     = bg_active;
        c[ImGuiCol_TitleBg]           = bg_panel;
        c[ImGuiCol_TitleBgActive]     = bg_raised;
        c[ImGuiCol_MenuBarBg]         = bg_panel;
        c[ImGuiCol_ScrollbarBg]       = ImVec4(bg.x, bg.y, bg.z, 0.6f);
        c[ImGuiCol_Button]            = accent_fnt;
        c[ImGuiCol_ButtonHovered]     = accent_dim;
        c[ImGuiCol_ButtonActive]      = accent_mid;
        c[ImGuiCol_Header]            = accent_fnt;
        c[ImGuiCol_HeaderHovered]     = accent_dim;
        c[ImGuiCol_HeaderActive]      = accent_mid;
        c[ImGuiCol_CheckMark]         = accent;
        c[ImGuiCol_SliderGrab]        = accent_dim;
        c[ImGuiCol_SliderGrabActive]  = accent;
        c[ImGuiCol_SeparatorHovered]  = accent_dim;
        c[ImGuiCol_SeparatorActive]   = accent;
        c[ImGuiCol_ResizeGrip]        = accent_fnt;
        c[ImGuiCol_ResizeGripHovered] = accent_dim;
        c[ImGuiCol_ResizeGripActive]  = accent;
        c[ImGuiCol_Tab]               = bg_panel;
        c[ImGuiCol_TabHovered]        = accent_dim;
        c[ImGuiCol_TabSelected]       = accent_fnt;
    }








    
    
    
    
    






    
    





    void UIManager::GlfwErrorCallback(int error, const char* description) {
        std::cerr << "GLFW Error " << error << ": " << description << std::endl;
    }

    bool UIManager::LoadConfig() {
        try {
            // Use AppData path for the config file
            std::string configDir = GetAppDataPath() + "\\config";
            std::filesystem::create_directories(configDir);
            
            // Store just the filename in the member variable
            config_file_ = "config.ini";
            
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Info("UIManager: Loading config from " + configDir + "\\" + config_file_);
            }
            
            // Pass just the filename to LoadFromFile. The Ex variant tells us
            // WHY a load failed so we can warn the user about a real permissions
            // or corruption problem while staying quiet on a benign first run.
            ConfigResult load = config_.LoadFromFileEx(config_file_);
            config_load_status_ = load.status;
            config_health_path_ = load.path;
            config_health_detail_ = load.detail;
            bool result = (load.status == ConfigStatus::Ok);

            // A missing config is normal (first run / fresh profile); only a
            // denied or corrupt load is worth interrupting the user for.
            if (load.status == ConfigStatus::AccessDenied ||
                load.status == ConfigStatus::Corrupt ||
                load.status == ConfigStatus::OtherError) {
                config_health_modal_pending_ = true;
                if (load.status == ConfigStatus::Corrupt && !load.quarantine_path.empty()) {
                    config_health_detail_ = "Corrupt config was moved to: " + load.quarantine_path;
                }
            }

            if (result) {
                UpdateUIFromConfig();

                // Populate the JawOpen constraint binding arrays from the
                // reserved-serial entries in the device binding maps.
                LoadJawBindingsFromConfig();

                // Set default OSC ports if they're not set
                if (config_.osc_send_port <= 0) {
                    config_.osc_send_port = 9000;
                }
                if (config_.osc_receive_port <= 0) {
                    config_.osc_receive_port = 9001;
                }

                // Migrate: write the freshly-loaded settings to the canonical
                // AppData location. If the config was read from a legacy spot
                // (older builds wrote next to the working dir), this moves it to
                // where the app and the Folders tab now expect it -- without
                // losing any of the user's existing settings. A failure here is
                // the classic "my settings don't stick" symptom, so record it.
                ConfigResult migrate = config_.SaveToFileEx(config_file_);
                NoteSaveResult(migrate);

                if (StayPutVR::Logger::IsInitialized()) {
                    StayPutVR::Logger::Info("UIManager: Config loaded successfully");
                }
            } else {
                // Set default OSC ports for new config
                config_.osc_send_port = 9000;
                config_.osc_receive_port = 9001;
                
                if (StayPutVR::Logger::IsInitialized()) {
                    StayPutVR::Logger::Error("UIManager: Failed to load config");
                }
            }
            return result;
        }
        catch (const std::exception& e) {
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Error("UIManager: Exception in LoadConfig: " + std::string(e.what()));
            }
            return false;
        }
    }

    bool UIManager::SaveConfig() {
        try {
            UpdateConfigFromUI();
            
            // Use AppData path for reference only
            std::string configDir = GetAppDataPath() + "\\config";
            std::filesystem::create_directories(configDir);
            
            // config_file_ should already be set to just the filename
            
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Info("UIManager: Saving config to " + configDir + "\\" + config_file_);
            }
            
            // Pass just the filename to SaveToFile. The Ex variant lets us latch
            // a persistent "settings are not being saved" warning when the write
            // is refused, instead of failing silently as before.
            ConfigResult save = config_.SaveToFileEx(config_file_);
            NoteSaveResult(save);
            if (!save.ok() && StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Error("UIManager: Failed to save config");
            }
            return save.ok();
        }
        catch (const std::exception& e) {
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Error("UIManager: Exception in SaveConfig: " + std::string(e.what()));
            }
            return false;
        }
    }

    void UIManager::NoteSaveResult(const ConfigResult& r) {
        if (r.ok()) {
            // A successful save clears any prior "not saving" warning -- e.g. the
            // user fixed the folder permissions and the next save went through.
            config_save_failing_ = false;
            return;
        }
        config_save_failing_ = true;
        config_health_path_ = r.path;
        config_health_detail_ = r.detail;
        // First time we discover saves are blocked, raise the explanatory modal.
        if (r.status == ConfigStatus::AccessDenied) {
            config_health_modal_pending_ = true;
        }
    }

    void UIManager::UpdateConfigFromUI() {
        // Boundary settings
        config_.warning_threshold = warning_threshold_;
        config_.bounds_threshold = position_threshold_;
        config_.disable_threshold = disable_threshold_;
        
        // Store device data from currently connected devices
        // DO NOT clear the maps - this would erase settings for disconnected devices
        
        for (const auto& device : device_positions_) {
            // Store device name if not empty
            if (!device.device_name.empty()) {
                config_.device_names[device.serial] = device.device_name;
            }
            
            // Store include_in_locking setting
            config_.device_settings[device.serial] = device.include_in_locking;
            
            // Store device role
            config_.device_roles[device.serial] = static_cast<int>(device.role);
            
            if (StayPutVR::Logger::IsInitialized() && device.role != DeviceRole::None) {
                StayPutVR::Logger::Debug("Saved role for device: " + device.serial + 
                    " -> role value: " + std::to_string(static_cast<int>(device.role)) + 
                    " (" + OSCManager::GetInstance().GetRoleString(device.role) + ")");
            }
        }
        
        if (StayPutVR::Logger::IsInitialized()) {
            StayPutVR::Logger::Info("Updated configuration from UI for " + 
                                    std::to_string(device_positions_.size()) + " devices");
        }
    }

    void UIManager::UpdateUIFromConfig() {
        if (StayPutVR::Logger::IsInitialized()) {
            Logger::Info("UpdateUIFromConfig: Beginning to update UI from loaded config");
        }
        
        // Boundary settings
        warning_threshold_ = config_.warning_threshold;
        position_threshold_ = config_.bounds_threshold;
        disable_threshold_ = config_.disable_threshold;
        
        // Update OSC status
        osc_enabled_ = config_.osc_enabled;
        
        if (StayPutVR::Logger::IsInitialized()) {
            if (!config_.device_roles.empty()) {
                Logger::Info("Found " + std::to_string(config_.device_roles.size()) + " device roles in config");
            } else {
                Logger::Warning("No device roles found in config");
            }
        }
        
        // Apply device names and settings
        for (auto& device : device_positions_) {
            // Apply device name if available
            auto nameIt = config_.device_names.find(device.serial);
            if (nameIt != config_.device_names.end()) {
                device.device_name = nameIt->second;
                if (StayPutVR::Logger::IsInitialized()) {
                    Logger::Info("Applied name for device: " + device.serial + " -> " + nameIt->second);
                }
            }
            
            // Apply include_in_locking setting if available
            auto settingIt = config_.device_settings.find(device.serial);
            if (settingIt != config_.device_settings.end()) {
                device.include_in_locking = settingIt->second;
                if (StayPutVR::Logger::IsInitialized()) {
                    Logger::Info("Applied include_in_locking setting for device: " + device.serial + 
                                " -> " + (settingIt->second ? "true" : "false"));
                }
            }
            
            // Apply device role if available
            auto roleIt = config_.device_roles.find(device.serial);
            if (roleIt != config_.device_roles.end()) {
                int roleValue = roleIt->second;
                device.role = static_cast<DeviceRole>(roleValue);
                
                if (StayPutVR::Logger::IsInitialized()) {
                    std::string roleName = "Unknown";
                    switch (device.role) {
                        case DeviceRole::None: roleName = "None"; break;
                        case DeviceRole::HMD: roleName = "HMD"; break;
                        case DeviceRole::LeftController: roleName = "LeftController"; break;
                        case DeviceRole::RightController: roleName = "RightController"; break;
                        case DeviceRole::Hip: roleName = "Hip"; break;
                        case DeviceRole::LeftFoot: roleName = "LeftFoot"; break;
                        case DeviceRole::RightFoot: roleName = "RightFoot"; break;
                        default: roleName = "Unknown"; break;
                    }
                    Logger::Info("Applied role for device: " + device.serial + 
                                " -> role value: " + std::to_string(roleValue) + 
                                " (Role: " + roleName + ")");
                }
            } else {
                if (StayPutVR::Logger::IsInitialized()) {
                    Logger::Warning("No role found in config for device: " + device.serial);
                }
            }
        }
        
        if (StayPutVR::Logger::IsInitialized()) {
            Logger::Info("UpdateUIFromConfig: Finished updating UI from config");
        }
    }

































} // namespace StayPutVR 