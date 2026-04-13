#pragma once

#include <functional>
#include <memory>
#include "../../../../common/Config.hpp"

namespace StayPutVR {

class PiShockManager;
class PiShockWebSocketManager;

class PiShockPanel {
public:
    PiShockPanel(Config& config,
                 std::unique_ptr<PiShockManager>& pishock_manager,
                 std::unique_ptr<PiShockWebSocketManager>& pishock_ws_manager,
                 std::function<void()> save_config);

    void Render();

private:
    Config& config_;
    std::unique_ptr<PiShockManager>& pishock_manager_;
    std::unique_ptr<PiShockWebSocketManager>& pishock_ws_manager_;
    std::function<void()> save_config_;
};

} // namespace StayPutVR
