#pragma once

#include <functional>
#include <memory>
#include "../../../../common/Config.hpp"

namespace StayPutVR {

class OpenShockManager;

class OpenShockPanel {
public:
    OpenShockPanel(Config& config,
                   std::unique_ptr<OpenShockManager>& openshock_manager,
                   std::function<void()> save_config);

    void Render();

private:
    Config& config_;
    std::unique_ptr<OpenShockManager>& openshock_manager_;
    std::function<void()> save_config_;
};

} // namespace StayPutVR
