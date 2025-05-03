#include "StayPutUI.h"
#include "StayPutDriver.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_stdlib.h>
#include <GLFW/glfw3.h>
#include <iostream>

StayPutUI::StayPutUI(StayPutDriver* driver) 
    : driver(driver), shouldExit(false) {
}

StayPutUI::~StayPutUI() {
    Stop();
}

void StayPutUI::Start() {
    shouldExit = false;
    uiThread = std::thread(&StayPutUI::RenderLoop, this);
}

void StayPutUI::Stop() {
    shouldExit = true;
    if (uiThread.joinable()) {
        uiThread.join();
    }
}

void StayPutUI::RenderLoop() {
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return;
    }

    // Create window
    GLFWwindow* window = glfwCreateWindow(800, 600, "StayPutVR Control Panel", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    // Initialize ImGui backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // Main render loop
    while (!shouldExit && !glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Start new ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Build UI
        BuildMainWindow();
        
        // Render
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }
    
    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
}

void StayPutUI::BuildMainWindow() {
    ImGui::Begin("StayPutVR Control Panel");

    BuildDevicePanel();
    ImGui::Separator();
    BuildControlPanel();
    ImGui::Separator();
    BuildFreezePanel();
    
    ImGui::End();
}

void StayPutUI::BuildDevicePanel() {
    if (ImGui::CollapsingHeader("Tracked Devices")) {
        auto devices = driver->GetTrackedDevices();
        for (const auto& pair : devices) {
            ImGui::Text("Device: %s (%s)", pair.first.c_str(), pair.second.deviceClass.c_str());
        }
    }
}

void StayPutUI::BuildControlPanel() {
    if (ImGui::CollapsingHeader("Controls")) {
        if (ImGui::Button("Start Tracking")) {
            driver->StartTracking();
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop Tracking")) {
            driver->StopTracking();
        }

        ImGui::InputText("Save File Path", &saveFilePath);
        if (ImGui::Button("Save Data")) {
            driver->SaveData(saveFilePath);
        }
    }
}

void StayPutUI::BuildFreezePanel() {
    if (ImGui::CollapsingHeader("Freeze Controls")) {
        auto devices = driver->GetTrackedDevices();
        for (const auto& pair : devices) {
            bool isSelected = std::find(selectedDevices.begin(), selectedDevices.end(), pair.first) != selectedDevices.end();
            if (ImGui::Checkbox(pair.first.c_str(), &isSelected)) {
                if (isSelected) {
                    selectedDevices.push_back(pair.first);
                } else {
                    selectedDevices.erase(std::remove(selectedDevices.begin(), selectedDevices.end(), pair.first), selectedDevices.end());
                }
            }
        }
    
        if (ImGui::Button("Freeze Selected")) {
            driver->FreezeTrackers(selectedDevices);
        }
        ImGui::SameLine();
        if (ImGui::Button("Unfreeze Selected")) {
            driver->UnfreezeTrackers(selectedDevices);
        }
    }
}
