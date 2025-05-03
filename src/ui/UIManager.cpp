#include "UIManager.hpp"
#include <iostream>
#include <string>
#include <format>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

extern std::atomic<bool> g_running;

namespace StayPutVR {

    UIManager::UIManager() : window_(nullptr), imgui_context_(nullptr), running_ptr_(&g_running) {
    }

    UIManager::~UIManager() {
        Shutdown();
    }

    bool UIManager::Initialize() {
        // Setup GLFW error callback
        glfwSetErrorCallback(UIManager::GlfwErrorCallback);
        
        // Initialize GLFW
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
        
        glfwMakeContextCurrent(window_);
        glfwSwapInterval(1); // Enable vsync
        
        // Initialize GLAD
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cerr << "Failed to initialize GLAD" << std::endl;
            return false;
        }
        
        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        imgui_context_ = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
        
        // Setup Dear ImGui style
        ImGui::StyleColorsDark();
        
        // Setup Platform/Renderer backends
        ImGui_ImplGlfw_InitForOpenGL(window_, true);
        ImGui_ImplOpenGL3_Init(glsl_version);
        
        // Set window close callback
        glfwSetWindowCloseCallback(window_, [](GLFWwindow* window) {
            g_running = false;
        });
        
        return true;
    }

    void UIManager::Update() {
        // Poll and handle events
        glfwPollEvents();
        
        // Check if window should close
        if (glfwWindowShouldClose(window_)) {
            *running_ptr_ = false;
        }
        
        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void UIManager::Render() {
        // Render the UI
        RenderMainWindow();
        
        // Rendering
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

    void UIManager::UpdateDevicePositions(const std::vector<std::shared_ptr<IVRDevice>>& devices) {
        device_positions_.clear();
        
        for (const auto& device : devices) {
            if (!device)
                continue;
            
            DevicePosition pos;
            pos.serial = device->GetSerial();
            pos.type = device->GetDeviceType();
            
            // Get pose from device
            vr::DriverPose_t pose = device->GetPose();
            
            // Store position
            pos.position[0] = pose.vecPosition[0];
            pos.position[1] = pose.vecPosition[1];
            pos.position[2] = pose.vecPosition[2];
            
            // Store rotation
            pos.rotation[0] = pose.qRotation.x;
            pos.rotation[1] = pose.qRotation.y;
            pos.rotation[2] = pose.qRotation.z;
            pos.rotation[3] = pose.qRotation.w;
            
            device_positions_.push_back(pos);
        }
    }

    void UIManager::RenderMainWindow() {
        // Create main window that fills the entire viewport
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        
        ImGui::Begin("StayPutVR Control Panel", nullptr, 
            ImGuiWindowFlags_NoDecoration | 
            ImGuiWindowFlags_NoMove | 
            ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoSavedSettings);
        
        // Title bar
        ImGui::Text("StayPutVR Control Panel");
        ImGui::Separator();
        
        // Device list
        RenderDeviceList();
        
        // Exit button
        if (ImGui::Button("Exit")) {
            *running_ptr_ = false;
        }
        
        ImGui::End();
    }

    void UIManager::RenderDeviceList() {
        ImGui::Text("Connected Devices: %zu", device_positions_.size());
        ImGui::Separator();
        
        if (ImGui::BeginTable("DevicesTable", 3, ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("Device Type");
            ImGui::TableSetupColumn("Serial");
            ImGui::TableSetupColumn("Position & Rotation");
            ImGui::TableHeadersRow();
            
            for (const auto& device : device_positions_) {
                ImGui::TableNextRow();
                
                // Device Type
                ImGui::TableNextColumn();
                const char* type_str = "Unknown";
                switch (device.type) {
                    case DeviceType::HMD: type_str = "HMD"; break;
                    case DeviceType::CONTROLLER: type_str = "Controller"; break;
                    case DeviceType::TRACKER: type_str = "Tracker"; break;
                    case DeviceType::TRACKING_REFERENCE: type_str = "Base Station"; break;
                }
                ImGui::Text("%s", type_str);
                
                // Serial
                ImGui::TableNextColumn();
                ImGui::Text("%s", device.serial.c_str());
                
                // Position & Rotation
                ImGui::TableNextColumn();
                ImGui::Text("Pos: (%.2f, %.2f, %.2f)", 
                    device.position[0], device.position[1], device.position[2]);
                ImGui::Text("Rot: (%.2f, %.2f, %.2f, %.2f)", 
                    device.rotation[0], device.rotation[1], 
                    device.rotation[2], device.rotation[3]);
            }
            
            ImGui::EndTable();
        }
    }

    void UIManager::GlfwErrorCallback(int error, const char* description) {
        std::cerr << "GLFW Error " << error << ": " << description << std::endl;
    }

} // namespace StayPutVR 