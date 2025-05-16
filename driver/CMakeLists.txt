cmake_minimum_required(VERSION 3.15)

# Collect GLAD sources
file(GLOB GLAD_SOURCES "${CMAKE_SOURCE_DIR}/thirdparty/glad/src/glad.c")

# Set INIH sources
set(INIH_SOURCES "${CMAKE_SOURCE_DIR}/thirdparty/inih/INIReader.h")

# Driver sources
file(GLOB_RECURSE DRIVER_SOURCES 
    "*.cpp"
    "*.hpp"
)

# Create driver library
add_library(driver_stayputvr SHARED 
    ${DRIVER_SOURCES}
    ${GLAD_SOURCES}
    ${INIH_SOURCES}
    "${CMAKE_SOURCE_DIR}/thirdparty/imgui/backends/imgui_impl_glfw.cpp"
    "${CMAKE_SOURCE_DIR}/thirdparty/imgui/backends/imgui_impl_opengl3.cpp"
)

target_include_directories(driver_stayputvr PRIVATE 
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(driver_stayputvr PRIVATE
    openvr_api.lib
    imgui
    glfw
    winmm.lib
    stayputvr_common
)

# Custom install target for our driver
install(CODE "message(STATUS \"Installing driver to SteamVR\")")

# Copy the driver binary
install(FILES "$<TARGET_FILE:driver_stayputvr>"
    DESTINATION "${STEAMVR_PATH}/drivers/stayputvr/bin/win64"
)

# Install driver manifest 
install(FILES "${CMAKE_SOURCE_DIR}/driver.vrdrivermanifest"
    DESTINATION "${STEAMVR_PATH}/drivers/stayputvr"
)

# Create and install configs directory
install(DIRECTORY "${CMAKE_BINARY_DIR}/configs"
    DESTINATION "${STEAMVR_PATH}/drivers/stayputvr"
) 