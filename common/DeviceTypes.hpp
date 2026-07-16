#pragma once
#include <string>
#include <cstdint>

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

        // --- Dataset-capture fields (DEVICE_UPDATE_V2 wire format) ---
        // Populated by the driver from the raw SteamVR pose. Legacy v1 messages
        // leave them at these defaults, so downstream consumers can always read
        // them safely.
        float velocity[3] = {0.0f, 0.0f, 0.0f};          // m/s, world space
        float angular_velocity[3] = {0.0f, 0.0f, 0.0f};  // rad/s, world space
        bool pose_valid = true;          // SteamVR bPoseIsValid at sample time
        uint8_t tracking_result = 0;     // vr::ETrackingResult (0 = unknown/legacy)

        // Sample times, stamped by the driver when GetRawTrackedDevicePoses was
        // read (NOT on pipe receipt, which would bake IPC jitter into every
        // label). One clock pair per driver frame, copied onto each device.
        //   sample_time_wall: seconds since Unix epoch (system_clock) — aligns
        //     this stream with other logs (frames, OSC, audio).
        //   sample_time_mono: seconds on the driver's steady_clock — monotonic,
        //     use for deltas/derivatives. 0.0 = legacy v1 message (no stamp).
        double sample_time_wall = 0.0;
        double sample_time_mono = 0.0;
    };
}
