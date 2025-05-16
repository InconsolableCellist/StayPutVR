#pragma once
#include <string>
#include <vector>
#include "DeviceTypes.hpp"

namespace StayPutVR {
    enum class MessageType {
        DEVICE_UPDATE,
        CONFIGURATION_UPDATE,
        COMMAND,
        HEARTBEAT,
        HANDSHAKE
    };

    struct IPCMessage {
        MessageType type;
        // Message-specific data would follow
    };

    struct DeviceUpdateMessage : IPCMessage {
        std::vector<DevicePositionData> devices;
    };

    // Other message types...
}
