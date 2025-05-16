#pragma once

#include <cstdlib>
#include <memory>

#include <openvr_driver.h>

#include "../common/IVRDriver.hpp"

extern "C" __declspec(dllexport) void* HmdDriverFactory(const char* interface_name, int* return_code);

namespace StayPutVR {
    std::shared_ptr<StayPutVR::IVRDriver> GetDriver();
} 