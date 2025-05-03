#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "../Driver/IVRDevice.hpp"

namespace StayPutVR {

    struct DevicePosition {
        std::string serial;
        DeviceType type;
        float position[3];
        float rotation[4]; // Quaternion (x, y, z, w)
    };

    class UIManager {
    public:
        UIManager();
        ~UIManager();

        bool Initialize();
        void Update();
        void Render();
        void Shutdown();
        
        // Update device positions from driver
        void UpdateDevicePositions(const std::vector<std::shared_ptr<IVRDevice>>& devices);
        
    private:
        // Main window
        GLFWwindow* window_;
        
        // ImGui contexts
        ImGuiContext* imgui_context_;
        
        // Window settings
        int window_width_ = 800;
        int window_height_ = 600;
        std::string window_title_ = "StayPutVR";
        
        // Device data
        std::vector<DevicePosition> device_positions_;
        
        // Static callbacks for GLFW
        static void GlfwErrorCallback(int error, const char* description);
        
        // UI elements
        void RenderMainWindow();
        void RenderDeviceList();
        
        // Flag for window closing
        std::atomic<bool>* running_ptr_;
    };

} 