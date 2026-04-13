#pragma once

#include <functional>
#include <memory>
#include "../../../../common/Config.hpp"

namespace StayPutVR {

class ButtplugManager;

class ButtplugPanel {
public:
    ButtplugPanel(Config& config,
                  std::unique_ptr<ButtplugManager>& buttplug_manager,
                  std::function<void()> save_config);

    void Render();

private:
    Config& config_;
    std::unique_ptr<ButtplugManager>& buttplug_manager_;
    std::function<void()> save_config_;
};

} // namespace StayPutVR
