cmake_minimum_required(VERSION 3.15)

# Set header-only files
set(HEADER_FILES
    Config.hpp
    DeviceTypes.hpp
    Logger.hpp
    OSCManager.hpp
    Audio.hpp
    PathUtils.hpp
    IVRDevice.hpp
    IPCProtocol.hpp
    IVRDriver.hpp
)

# Common library for shared code between driver and application
add_library(stayputvr_common STATIC
    Config.cpp
    Audio.cpp
    Logger.cpp
    OSCManager.cpp
    ${HEADER_FILES}
)

target_include_directories(stayputvr_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Set runtime library settings - MUST match the same settings as the application and driver
if(MSVC)
    # For the release build, use the release C Runtime
    set_target_properties(stayputvr_common PROPERTIES 
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
    # Ensure release builds use MD instead of MT
    target_compile_options(stayputvr_common PRIVATE
        $<$<CONFIG:Release>:/MD>
        $<$<CONFIG:Debug>:/MDd>
    )
endif()

# Link with Windows libraries for audio
target_link_libraries(stayputvr_common PRIVATE
    winmm.lib
    ws2_32.lib
) 