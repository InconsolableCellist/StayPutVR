cmake_minimum_required(VERSION 3.15)

# Set header-only files
set(HEADER_FILES
    Config.hpp
    DeviceTypes.hpp
    Logger.hpp
    OSCManager.hpp
    OSCQueryServer.hpp
    Audio.hpp
    PathUtils.hpp
    IVRDevice.hpp
    IPCProtocol.hpp
    IVRDriver.hpp
    HttpClient.hpp
    WebSocketClient.hpp
    IShockDeviceManager.hpp
    ShockDeviceBase.hpp
    AsyncWorkQueue.hpp
)

# Common library for shared code between driver and application
add_library(stayputvr_common STATIC
    Config.cpp
    Audio.cpp
    Logger.cpp
    OSCManager.cpp
    OSCQueryServer.cpp
    HttpClient.cpp
    WebSocketClient.cpp
    ShockDeviceBase.cpp
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

# Platform networking/audio/threading dependencies
if(WIN32)
    # Link with Windows libraries for audio + sockets.
    # iphlpapi is needed by the vendored mdns.h (OSCQuery) for adapter enumeration.
    target_link_libraries(stayputvr_common PRIVATE
        winmm.lib
        ws2_32.lib
        iphlpapi.lib
    )
else()
    find_package(Threads REQUIRED)
    target_link_libraries(stayputvr_common PRIVATE
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
endif()