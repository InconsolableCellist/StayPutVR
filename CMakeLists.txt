cmake_minimum_required(VERSION 3.15)
project(StayPutVR VERSION 1.3.0)

# Specify C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enforce consistent runtime library settings across all targets
if(MSVC)
    # Use dynamic runtime (/MD or /MDd) for both Debug and Release
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    
    # Force use of dynamic CRT for all builds
    # This ensures /MD for Release and /MDd for Debug
    string(REPLACE "/MT" "/MD" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
    string(REPLACE "/MT" "/MD" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
    string(REPLACE "/MTd" "/MDd" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    string(REPLACE "/MTd" "/MDd" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

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
    thirdparty
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

# Add installer build target
set(INSTALLER_DIR "${CMAKE_SOURCE_DIR}/installer")
set(INSTALLER_SCRIPT "${INSTALLER_DIR}/app_installer.nsi")
set(INSTALLER_OUTPUT "${INSTALLER_DIR}/StayPutVR_Setup.exe")
set(NSIS_DIR "${INSTALLER_DIR}/NSIS")
set(NSIS_EXE "${NSIS_DIR}/makensis.exe")

# Add a custom target to prepare files for the installer
add_custom_target(prepare_installer
    COMMAND ${CMAKE_COMMAND} -E make_directory "${INSTALLER_DIR}/build/bin"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${INSTALLER_DIR}/application/resources"
    COMMAND ${CMAKE_COMMAND} -E copy 
        "$<TARGET_FILE:stayputvr_app>" 
        "${INSTALLER_DIR}/build/bin/stayputvr_app.exe"
    COMMAND ${CMAKE_COMMAND} -E copy 
        "$<TARGET_FILE:driver_stayputvr>" 
        "${INSTALLER_DIR}/build/driver_stayputvr.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_directory 
        "${CMAKE_SOURCE_DIR}/application/resources" 
        "${INSTALLER_DIR}/application/resources"
    COMMAND ${CMAKE_COMMAND} -E copy 
        "${CMAKE_SOURCE_DIR}/driver.vrdrivermanifest" 
        "${INSTALLER_DIR}/driver.vrdrivermanifest"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/LICENSE" 
        "${INSTALLER_DIR}/LICENSE.txt"
    DEPENDS stayputvr_app driver_stayputvr
    COMMENT "Copying files for installer package"
)

# Add a custom target to build the installer - using portable NSIS
add_custom_target(build_installer
    # Set NSISDIR environment variable to point to our portable NSIS directory
    COMMAND ${CMAKE_COMMAND} -E env "NSISDIR=${NSIS_DIR}" 
        "${NSIS_EXE}" "${INSTALLER_SCRIPT}"
    DEPENDS prepare_installer
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Building installer package with portable NSIS"
)
