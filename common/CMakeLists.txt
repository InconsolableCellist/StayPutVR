cmake_minimum_required(VERSION 3.15)

# Set header-only files
set(HEADER_FILES
    PathUtils.hpp
    DeviceTypes.hpp
    IPCProtocol.hpp
)

# Common library for shared code between driver and application
add_library(stayputvr_common STATIC
    Config.cpp
    Audio.cpp
    Logger.cpp
    ${HEADER_FILES}
)

target_include_directories(stayputvr_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link with Windows libraries for audio
target_link_libraries(stayputvr_common PRIVATE
    winmm.lib
) 