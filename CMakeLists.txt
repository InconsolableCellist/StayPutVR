cmake_minimum_required(VERSION 3.15)
project(StayPutVR VERSION 1.4.1)

# Specify C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Generate git_hash.h (STAYPUTVR_GIT_HASH) for the build string shown in the
# splash and Settings > About. Generate once at configure time so the header
# exists for the first compile, then refresh it on every build via a target.
set(STAYPUTVR_GIT_HASH_HEADER "${CMAKE_BINARY_DIR}/git_hash.h")
execute_process(COMMAND ${CMAKE_COMMAND}
    -DSRC_DIR=${CMAKE_SOURCE_DIR}
    -DOUT_FILE=${STAYPUTVR_GIT_HASH_HEADER}
    -P "${CMAKE_SOURCE_DIR}/cmake/GitHash.cmake")
add_custom_target(git_hash_header ALL
    COMMAND ${CMAKE_COMMAND}
        -DSRC_DIR=${CMAKE_SOURCE_DIR}
        -DOUT_FILE=${STAYPUTVR_GIT_HASH_HEADER}
        -P "${CMAKE_SOURCE_DIR}/cmake/GitHash.cmake"
    BYPRODUCTS "${STAYPUTVR_GIT_HASH_HEADER}"
    COMMENT "Updating git hash header")
# Make the generated header reachable as #include "git_hash.h" everywhere.
include_directories(${CMAKE_BINARY_DIR})

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

if(WIN32)
    # =========================================================================
    # Windows: full release build (SteamVR driver + overlay application)
    # =========================================================================

    # Set paths
    set(OPENVR_SDK_PATH "C:/apps/programming/openvr-2.15.6" CACHE PATH "Path to OpenVR SDK")
    set(STEAM_PATH "C:/games/Steam" CACHE PATH "Path to Steam installation")
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
        thirdparty/cpp-httplib
        thirdparty/mdns
        thirdparty
        ${OPENVR_SDK_PATH}/headers
    )

    # Add link directories
    link_directories(${OPENVR_SDK_PATH}/lib/win64)

    # Add subdirectories for our components
    add_subdirectory(common)
    add_subdirectory(driver)
    add_subdirectory(application)
    add_dependencies(stayputvr_app git_hash_header)

    # Create a simple batch file to launch the application
    file(WRITE "${CMAKE_BINARY_DIR}/launch_stayputvr.bat"
        "@echo off\n"
        "echo Starting StayPutVR Application...\n"
        "start \"\" \"${CMAKE_INSTALL_PREFIX}/bin/stayputvr_app.exe\"\n"
    )

    install(FILES "${CMAKE_BINARY_DIR}/launch_stayputvr.bat"
        DESTINATION "${CMAKE_INSTALL_PREFIX}"
    )

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

else()
    # =========================================================================
    # Linux: development / OSC-simulation build (overlay GUI + OSC only).
    # No SteamVR driver. PiShock/Twitch/audio are stubbed. Lets the prefab be
    # iterated on by simulating OSC without a headset. See README "Development".
    # =========================================================================
    message(STATUS "StayPutVR: configuring Linux development build (GUI + OSC, no driver)")

    include(FetchContent)

    # GLFW headers only: we link against the system-installed runtime library
    # (libglfw.so.3.x) and just need the public headers to compile.
    FetchContent_Declare(glfw_headers
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE
    )
    FetchContent_GetProperties(glfw_headers)
    if(NOT glfw_headers_POPULATED)
        FetchContent_Populate(glfw_headers)
    endif()

    # Link against the system-installed GLFW runtime. This immutable/overlay OS
    # ships GLFW where the *system linker* (/usr/bin/ld) resolves it fine, but
    # this CMake/make build can't stat() it. So we must NOT:
    #   - use find_library/if(EXISTS) (stat fails -> "not found")
    #   - link by absolute path (make treats it as a prerequisite -> "no rule")
    #   - use target_link_directories (CMake strips /usr/lib64 as "implicit")
    # Instead inject "-L<dir> -l:<soname>" as raw link OPTIONS, which CMake
    # passes verbatim and the system linker resolves. Overridable per-distro.
    set(STAYPUT_GLFW_SONAME "libglfw.so.3" CACHE STRING "GLFW runtime soname to link against")

    # Search both the native host lib dir and the distrobox/toolbox host mount
    # (/run/host/usr/lib64), since the container that runs cmake here lacks GLFW
    # but the host does. ld silently ignores -L dirs that don't exist.
    set(STAYPUT_GLFW_LIBDIRS "/usr/lib64;/run/host/usr/lib64;/usr/lib/x86_64-linux-gnu;/usr/local/lib64;/usr/local/lib"
        CACHE STRING "Directories to search for the GLFW runtime library")
    set(_glfw_link_opts "")
    foreach(_dir IN LISTS STAYPUT_GLFW_LIBDIRS)
        list(APPEND _glfw_link_opts "-L${_dir}")
    endforeach()
    list(APPEND _glfw_link_opts "-l:${STAYPUT_GLFW_SONAME}")

    # Provide an INTERFACE target named "glfw" so existing target_link_libraries
    # references (in imgui and application) resolve unchanged.
    add_library(glfw INTERFACE)
    target_include_directories(glfw INTERFACE "${glfw_headers_SOURCE_DIR}/include")
    target_link_options(glfw INTERFACE ${_glfw_link_opts})
    message(STATUS "StayPutVR: linking system GLFW: ${_glfw_link_opts}")

    # Add Dear ImGui (imgui CMakeLists has a Linux branch for GL/glfw linking)
    add_subdirectory(thirdparty/imgui)

    include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}
        thirdparty/imgui
        thirdparty/glad/include
        thirdparty/json/include
        thirdparty/json/single_include
        thirdparty/cpp-httplib
        thirdparty/mdns
        thirdparty
        ${glfw_headers_SOURCE_DIR}/include
    )

    # Force-include the platform compatibility shim (min/max etc.) for our own
    # code only. Added after imgui so the imgui target is unaffected. The shim
    # is a no-op for C sources (e.g. glad.c) via an __cplusplus guard.
    add_compile_options(-include "${CMAKE_SOURCE_DIR}/common/PlatformCompat.hpp")

    # Build only the shared library and the GUI application (no driver).
    add_subdirectory(common)
    add_subdirectory(application)
    add_dependencies(stayputvr_app git_hash_header)
endif()
