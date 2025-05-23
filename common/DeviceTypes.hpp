#pragma once
#include <string>

namespace StayPutVR {
    // Shared device type definitions that both driver and application use
    enum class DeviceType {
        HMD,
        CONTROLLER,
        TRACKER,
        TRACKING_REFERENCE,
        UNKNOWN
    };

    // Forward declaration of DeviceRole from OSCManager.hpp to avoid circular dependencies
    enum class DeviceRole;

    // Shared device position structure for IPC
    struct DevicePositionData {
        std::string serial;
        DeviceType type;
        float position[3];
        float rotation[4];
        bool connected;
        DeviceRole role = static_cast<DeviceRole>(0); // Default to None
    };
}
