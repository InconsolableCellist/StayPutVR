cmake_minimum_required(VERSION 3.15)
project(StayPutVR VERSION 0.1.0)

# Specify C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set paths
set(OPENVR_SDK_PATH "C:/apps/programming/openvr-2.5.1" CACHE PATH "Path to OpenVR SDK")
set(STEAM_PATH "C:/games/online/Steam" CACHE PATH "Path to Steam installation")
set(STEAMVR_PATH "${STEAM_PATH}/steamapps/common/SteamVR" CACHE PATH "Path to SteamVR installation")

# Verify OpenVR SDK exists
if(NOT EXISTS "${OPENVR_SDK_PATH}/headers/openvr.h")
    message(FATAL_ERROR "OpenVR SDK not found at ${OPENVR_SDK_PATH}/headers/openvr.h")
endif()

# Add OpenVR includes and libraries
include_directories(${OPENVR_SDK_PATH}/headers)
link_directories(${OPENVR_SDK_PATH}/lib/win64)

# Add Dear ImGui
add_subdirectory(thirdparty/imgui)
include_directories(thirdparty/imgui)

# Add GLAD
include_directories(thirdparty/glad/include)
file(GLOB GLAD_SOURCES "thirdparty/glad/src/glad.c")

# Add GLFW
include_directories(thirdparty/glfw-3.4/include)
add_subdirectory(thirdparty/glfw-3.4)

# Add nlohmann/json
include_directories(thirdparty/json/include)

# Add sources
set(SOURCES
    src/main.cpp
    src/Native/DriverFactory.cpp
    src/Driver/VRDriver.cpp
    src/UI/UIManager.cpp
    ${GLAD_SOURCES}
)

# Create driver library
add_library(driver_stayputvr SHARED ${SOURCES})
target_include_directories(driver_stayputvr PRIVATE 
    src
    thirdparty
)

target_link_libraries(driver_stayputvr PRIVATE
    openvr_api.lib
    imgui
    glfw
)

# Set install paths
set(STEAM_PATH "C:/games/online/Steam" CACHE PATH "Path to Steam installation")
set(STEAMVR_PATH "${STEAM_PATH}/steamapps/common/SteamVR" CACHE PATH "Path to SteamVR installation")

# Install target
install(TARGETS driver_stayputvr
    RUNTIME DESTINATION "${STEAMVR_PATH}/drivers/stayputvr/bin/win64"
)

# Install driver manifest 
install(FILES "driver.vrdrivermanifest"
    DESTINATION "${STEAMVR_PATH}/drivers/stayputvr"
)

# Add a convenient target for launching in development
add_custom_target(install_and_run
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target install
    COMMAND ${CMAKE_COMMAND} -E chdir "${STEAM_PATH}/bin" vrpathreg adddriver "${STEAMVR_PATH}/drivers/stayputvr"
    DEPENDS driver_stayputvr
)
