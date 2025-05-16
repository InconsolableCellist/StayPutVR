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

# Configure third-party libraries to not install globally
# Disable all library installation options
set(BUILD_SHARED_LIBS OFF)
set(GLFW_INSTALL OFF CACHE BOOL "Generate installation target" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Build the GLFW example programs" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "Build the GLFW test programs" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "Build the GLFW documentation" FORCE)

# Add third-party libraries
# Add Dear ImGui
add_subdirectory(thirdparty/imgui)
# Add GLFW
add_subdirectory(thirdparty/glfw-3.4)

# Add common include directories for third-party libraries
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    thirdparty/imgui
    thirdparty/glad/include
    thirdparty/glfw-3.4/include
    thirdparty/json/include
    thirdparty/json/single_include
    thirdparty/inih
    ${OPENVR_SDK_PATH}/headers
)

# Add link directories
link_directories(${OPENVR_SDK_PATH}/lib/win64)

# Add subdirectories for our components
add_subdirectory(common)
add_subdirectory(driver)
add_subdirectory(application)

# Create a simple batch file to launch the application
if(WIN32)
    file(WRITE "${CMAKE_BINARY_DIR}/launch_stayputvr.bat" 
        "@echo off\n"
        "echo Starting StayPutVR Application...\n"
        "start \"\" \"${CMAKE_INSTALL_PREFIX}/bin/stayputvr_app.exe\"\n"
    )
    
    install(FILES "${CMAKE_BINARY_DIR}/launch_stayputvr.bat"
        DESTINATION "${CMAKE_INSTALL_PREFIX}"
    )
endif()

# Add a convenient target for launching in development
add_custom_target(install_and_run
    COMMAND ${CMAKE_COMMAND} -DCMAKE_INSTALL_CONFIG_NAME=$<CONFIG> -P "${CMAKE_BINARY_DIR}/cmake_install.cmake"
    COMMAND ${CMAKE_COMMAND} -E chdir "${STEAM_PATH}/bin" vrpathreg.exe adddriver "${STEAMVR_PATH}/drivers/stayputvr"
    DEPENDS driver_stayputvr
    COMMENT "Installing and registering driver with SteamVR"
)
