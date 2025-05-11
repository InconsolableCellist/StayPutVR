cmake_minimum_required(VERSION 3.15)
project(StayPutVR VERSION 0.1.0)

# Specify C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set our install prefix explicitly to avoid Program Files issues
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/local_install" CACHE PATH "Installation directory")

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

# Configure third-party libraries to not install globally
# Disable all library installation options
set(BUILD_SHARED_LIBS OFF)
set(GLFW_INSTALL OFF CACHE BOOL "Generate installation target" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Build the GLFW example programs" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "Build the GLFW test programs" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "Build the GLFW documentation" FORCE)

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
include_directories(thirdparty/json/single_include)

# Add inih library
include_directories(thirdparty/inih)
set(INIH_SOURCES "thirdparty/inih/INIReader.h")

# Add sources
set(SOURCES
    src/main.cpp
    src/Native/DriverFactory.cpp
    src/Driver/VRDriver.cpp
    src/ui/UIManager.cpp
    src/Config.cpp
    src/Logger.cpp
    src/Audio.cpp
    ${GLAD_SOURCES}
    ${INIH_SOURCES}
)

# Create driver library
add_library(driver_stayputvr SHARED ${SOURCES})
target_include_directories(driver_stayputvr PRIVATE 
    src
    thirdparty
)

# Add ImGui backends
target_sources(driver_stayputvr PRIVATE
    thirdparty/imgui/backends/imgui_impl_glfw.cpp
    thirdparty/imgui/backends/imgui_impl_opengl3.cpp
)

target_link_libraries(driver_stayputvr PRIVATE
    openvr_api.lib
    imgui
    glfw
    winmm.lib
)

# Custom install target for our driver only
install(CODE "message(STATUS \"Installing driver to SteamVR\")")

# Copy the driver binary
install(FILES "$<TARGET_FILE:driver_stayputvr>"
    DESTINATION "${STEAMVR_PATH}/drivers/stayputvr/bin/win64"
)

# Install driver manifest 
install(FILES "driver.vrdrivermanifest"
    DESTINATION "${STEAMVR_PATH}/drivers/stayputvr"
)

# Create and install configs directory
install(DIRECTORY "${CMAKE_BINARY_DIR}/configs"
    DESTINATION "${STEAMVR_PATH}/drivers/stayputvr"
)

# Add a convenient target for launching in development
add_custom_target(install_and_run
    COMMAND ${CMAKE_COMMAND} -DCMAKE_INSTALL_CONFIG_NAME=$<CONFIG> -P "${CMAKE_BINARY_DIR}/cmake_install.cmake"
    COMMAND ${CMAKE_COMMAND} -E chdir "${STEAM_PATH}/bin" vrpathreg.exe adddriver "${STEAMVR_PATH}/drivers/stayputvr"
    DEPENDS driver_stayputvr
    COMMENT "Installing and registering driver with SteamVR"
)
