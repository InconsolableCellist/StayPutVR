cmake_minimum_required(VERSION 3.15)

# Collect GLAD sources
file(GLOB GLAD_SOURCES "${CMAKE_SOURCE_DIR}/thirdparty/glad/src/glad.c")

# Application sources
file(GLOB_RECURSE APP_SOURCES
    "*.cpp"
    "*.hpp"
)

if(WIN32)
    # Set INIH sources (header-only; listed so IDEs surface it)
    set(INIH_SOURCES "${CMAKE_SOURCE_DIR}/thirdparty/inih/INIReader.h")

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

else()
    # Linux development build: no .rc resource, no OpenVR, no winmm; GL via GLAD.
    add_executable(stayputvr_app
        ${APP_SOURCES}
        ${GLAD_SOURCES}
        "${CMAKE_SOURCE_DIR}/thirdparty/imgui/backends/imgui_impl_glfw.cpp"
        "${CMAKE_SOURCE_DIR}/thirdparty/imgui/backends/imgui_impl_opengl3.cpp"
    )

    target_include_directories(stayputvr_app PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    find_package(Threads REQUIRED)
    target_link_libraries(stayputvr_app PRIVATE
        imgui
        glfw
        stayputvr_common
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )

    install(TARGETS stayputvr_app
        RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
    )
endif()

# Stage the resources (logo, fonts, whats_new.md, effigy, *.wav) next to the
# built executable so running straight from the build tree -- without the
# installer -- finds them. GetResourcesPath() checks <exe dir>/resources first.
add_custom_command(TARGET stayputvr_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:stayputvr_app>/resources"
    COMMENT "Staging resources next to stayputvr_app"
)
