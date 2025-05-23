cmake_minimum_required(VERSION 3.15)

# Collect GLAD sources
file(GLOB GLAD_SOURCES "${CMAKE_SOURCE_DIR}/thirdparty/glad/src/glad.c")

# Set INIH sources
set(INIH_SOURCES "${CMAKE_SOURCE_DIR}/thirdparty/inih/INIReader.h")

# Application sources
file(GLOB_RECURSE APP_SOURCES 
    "*.cpp"
    "*.hpp"
)

# Create application executable with WIN32 flag to disable console
add_executable(stayputvr_app WIN32
    ${APP_SOURCES}
    ${GLAD_SOURCES}
    ${INIH_SOURCES}
    "${CMAKE_SOURCE_DIR}/thirdparty/imgui/backends/imgui_impl_glfw.cpp"
    "${CMAKE_SOURCE_DIR}/thirdparty/imgui/backends/imgui_impl_opengl3.cpp"
    resources.rc
)

target_include_directories(stayputvr_app PRIVATE 
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(stayputvr_app PRIVATE
    openvr_api.lib
    imgui
    glfw
    winmm.lib
    stayputvr_common
)

# Set runtime library settings for release builds
if(MSVC)
    # For the release build, use the dynamic C Runtime (DLL)
    set_target_properties(stayputvr_app PROPERTIES 
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
    # Ensure consistent runtime settings with common library
    target_compile_options(stayputvr_app PRIVATE
        $<$<CONFIG:Release>:/MD>
        $<$<CONFIG:Debug>:/MDd>
    )
endif()

# Install application
install(TARGETS stayputvr_app
    RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
)

# Copy resources
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources/"
    DESTINATION "${CMAKE_INSTALL_PREFIX}/resources"
    FILES_MATCHING PATTERN "*.*"
) 