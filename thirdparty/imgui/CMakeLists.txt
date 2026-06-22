cmake_minimum_required(VERSION 3.15)
project(imgui)

# Create static library
add_library(imgui STATIC
    imgui.cpp
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    misc/cpp/imgui_stdlib.cpp
    backends/imgui_impl_glfw.cpp
    backends/imgui_impl_opengl3.cpp
)

# Include directories
target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/misc/cpp
)

# Link against OpenGL
if(WIN32)
    find_package(OpenGL REQUIRED)
    target_link_libraries(imgui PUBLIC
        OpenGL::GL
        glfw
    )
else()
    # Linux dev build: the imgui_impl_opengl3 backend uses its own runtime GL
    # loader, so no -lGL link is required. GLFW is the system runtime (imported).
    target_link_libraries(imgui PUBLIC
        glfw
        ${CMAKE_DL_LIBS}
    )
endif()