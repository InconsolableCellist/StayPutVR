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
                
                // Explicitly set up callbacks
                OSCManager::GetInstance().SetLockCallback(
                    [this](OSCDeviceType device, bool locked) {
                        OnDeviceLocked(device, locked);
                    }
                );
                
                OSCManager::GetInstance().SetIncludeCallback(
                    [this](OSCDeviceType device, bool include) {
                        OnDeviceIncluded(device, include);
                    }
                );
                
                // Set up OSC callbacks in Initialize() method
                OSCManager::GetInstance().SetGlobalLockCallback(
                    [this](bool lock) {
                        if (Logger::IsInitialized()) {
                            Logger::Info("Global " + std::string(lock ? "lock" : "unlock") + " triggered via OSC");
                        }
                        // Use the same method the UI uses, which handles countdown and sound effects
                        ActivateGlobalLock(lock);
                    }
                );
                
                OSCManager::GetInstance().SetGlobalOutOfBoundsCallback(
                    [this](bool triggered) {
                        if (!config_.osc_global_out_of_bounds_enabled) {
                            return;
                        }
                        if (Logger::IsInitialized()) {
                            Logger::Info("Global out-of-bounds triggered via OSC");
                        }
                        TriggerGlobalOutOfBoundsActions();
                    }
                );
                
                OSCManager::GetInstance().SetBiteCallback(
                    [this](bool triggered) {
                        if (!config_.osc_bite_enabled) {
                            return;
                        }
                        if (Logger::IsInitialized()) {
                            Logger::Info("Bite triggered via OSC");
                        }
                        TriggerBiteActions();
                    }
                );

                OSCManager::GetInstance().SetAvatarChangeCallback(
                    [this]() {
                        HandleAvatarChange();
                    }
                );

                OSCManager::GetInstance().SetShockCallback(
                    [this](bool triggered) {
                        bool enabled; float intensity, duration;
                        {
                            auto cfg_lock = config_.ReadLock();
                            enabled = config_.osc_shock_enabled;
                            intensity = config_.osc_shock_intensity;
                            duration = config_.osc_shock_duration;
                        }
                        if (!enabled) {
                            return;
                        }
                        if (Logger::IsInitialized()) {
                            Logger::Info("Shock param triggered via OSC");
                        }
                        TriggerExternalShock(intensity, duration, "Shock param");
                    }
                );
                
                OSCManager::GetInstance().SetEStopStretchCallback(
                    [this](float stretch_value) {
                        if (!config_.osc_estop_stretch_enabled) {
                            return;
                        }
                        if (Logger::IsInitialized()) {
                            Logger::Info("Emergency stop stretch triggered via OSC with value: " + std::to_string(stretch_value) + " - entering emergency stop mode");
                        }
                        
                        // Enter emergency stop mode
                        emergency_stop_active_ = true;
                        
                        // Unlock all devices immediately
                        ActivateGlobalLock(false);
                        
                        // Also unlock any individually locked devices
                        for (auto& device : device_positions_) {
                            if (device.locked) {
                                LockDevicePosition(device.serial, false);
                            }
                        }
                        
                        if (Logger::IsInitialized()) {
                            Logger::Warning("EMERGENCY STOP MODE ACTIVE - All actions disabled until reset");
                        }
                    }
                );
                
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

        // Release the effigy GL texture (Devices > Visual).
        if (effigy_tex_ != 0) {
            GLuint t = effigy_tex_;
            glDeleteTextures(1, &t);
            effigy_tex_ = 0;
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
            
            // Pass just the filename to LoadFromFile
            bool result = config_.LoadFromFile(config_file_);
            if (result) {
                UpdateUIFromConfig();

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
                // losing any of the user's existing settings.
                config_.SaveToFile(config_file_);

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
            
            // Pass just the filename to SaveToFile
            bool result = config_.SaveToFile(config_file_);
            if (!result && StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Error("UIManager: Failed to save config");
            }
            return result;
        }
        catch (const std::exception& e) {
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Error("UIManager: Exception in SaveConfig: " + std::string(e.what()));
            }
            return false;
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