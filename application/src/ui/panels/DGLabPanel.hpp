#pragma once

#include <functional>
#include <memory>
#include "../../../../common/Config.hpp"

namespace StayPutVR {

class DGLabManager;

class DGLabPanel {
public:
    DGLabPanel(Config& config,
               std::unique_ptr<DGLabManager>& dglab_manager,
               std::function<void()> save_config);

    void Render();

private:
    void RenderQrCode(const std::string& payload);

    Config& config_;
    std::unique_ptr<DGLabManager>& dglab_manager_;
    std::function<void()> save_config_;

    // Test controls (session-local, not persisted)
    float test_intensity_ = 0.25f;
    float test_duration_ = 1.0f;
};

} // namespace StayPutVR
