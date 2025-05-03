#pragma once
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <vector>

class StayPutDriver;

class StayPutUI {
public:
    StayPutUI(StayPutDriver* driver);
    ~StayPutUI();
    
    void Start();
    void Stop();
    
private:
    void RenderLoop();
    void BuildMainWindow();
    void BuildDevicePanel();
    void BuildControlPanel();
    void BuildFreezePanel();
    
    StayPutDriver* driver;
    std::thread uiThread;
    std::atomic<bool> shouldExit;
    
    // UI state
    std::string saveFilePath;
    std::vector<std::string> selectedDevices;
};
