cmake_minimum_required(VERSION 3.20)
project(VanityArc)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# A plain "cmake -B build" would otherwise build without optimisation
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(glfw3 REQUIRED)
find_package(OpenGL REQUIRED)

# The layer-shell overlay window: Wayland client + EGL, and the protocol code made from draw/wayland/*.xml
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client wayland-egl egl)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
set(LAYER_XML ${CMAKE_SOURCE_DIR}/draw/wayland/wlr-layer-shell-unstable-v1.xml)
set(GEN_DIR ${CMAKE_BINARY_DIR}/generated)
add_custom_command(OUTPUT ${GEN_DIR}/wlr-layer-shell-client.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR}
        COMMAND ${WAYLAND_SCANNER} client-header ${LAYER_XML} ${GEN_DIR}/wlr-layer-shell-client.h
        DEPENDS ${LAYER_XML})
add_custom_command(OUTPUT ${GEN_DIR}/wlr-layer-shell-client.c
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR}
        COMMAND ${WAYLAND_SCANNER} private-code ${LAYER_XML} ${GEN_DIR}/wlr-layer-shell-client.c
        DEPENDS ${LAYER_XML})

# --- 1. DEFINE SOURCES FIRST ---
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/imgui")

set(IMGUI_SOURCES
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
        gamestate/GameState.cpp
        gamestate/Fast.cpp
        gamestate/Tracker.cpp
        gamestate/Tracker.h
        utils/log.h
        gamestate/Probe.cpp
        utils/names.cpp
        utils/names.h
        utils/labels.cpp
        utils/labels.h
        utils/labeler.cpp
        utils/labeler.h
        utils/gamescope.cpp
        utils/gamescope.h
        utils/gameview.h
        gamestate/GameState.h
        utils/localUtil.h
        utils/gameUtil.h
        memory/memory.cpp
        draw/ESP.h
        draw/Overlay.h
        draw/OverlayBackend.cpp
        draw/OverlayBackend.h
        draw/LayerWindow.cpp
        draw/LayerWindow.h
        ${GEN_DIR}/wlr-layer-shell-client.c
        ${GEN_DIR}/wlr-layer-shell-client.h
        draw/W2S.h
)

# --- 2. ADD EXECUTABLE WITH IMGUI SOURCES ---
add_executable(VanityArc
        main.cpp
        config.h
        memory/memory.h
        ${IMGUI_SOURCES}
)

# --- 3. INCLUDES AND LINKS ---
target_include_directories(VanityArc PRIVATE
        ${GEN_DIR}
        ${CMAKE_SOURCE_DIR}
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)

target_link_libraries(VanityArc PRIVATE glfw OpenGL::GL PkgConfig::WAYLAND)
# --- Headless tests (they need no game and no display): cmake -B cmake-build-tests -DVANITYARC_TESTS=ON
option(VANITYARC_TESTS "Build the headless tests" OFF)
if(VANITYARC_TESTS)
    enable_testing()
    function(vanityarc_test name)
        add_executable(${name} tests/${name}.cpp ${ARGN})
        target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR} ${IMGUI_DIR} ${IMGUI_DIR}/backends)
        add_test(NAME ${name} COMMAND ${name})
    endfunction()
    vanityarc_test(key_test)
    vanityarc_test(scatter_test memory/memory.cpp)
    vanityarc_test(draw_test)
    vanityarc_test(settings_test)
    vanityarc_test(offsets_test)
    vanityarc_test(gamescope_test utils/gamescope.cpp)
    vanityarc_test(gamestate_test gamestate/GameState.cpp gamestate/Fast.cpp gamestate/Tracker.cpp gamestate/Probe.cpp utils/gamescope.cpp
            utils/names.cpp utils/labels.cpp utils/labeler.cpp memory/memory.cpp)
    target_link_libraries(gamestate_test PRIVATE pthread)
    vanityarc_test(names_test utils/names.cpp gamestate/Probe.cpp memory/memory.cpp)
    vanityarc_test(labels_test utils/names.cpp utils/labels.cpp utils/labeler.cpp memory/memory.cpp)
    vanityarc_test(fuzz_test utils/names.cpp utils/labels.cpp utils/labeler.cpp memory/memory.cpp)
    vanityarc_test(menu_test utils/labels.cpp
            ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
            ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
    target_link_libraries(menu_test PRIVATE glfw OpenGL::GL)
    vanityarc_test(esp_test utils/labels.cpp
            ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
            ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
    target_link_libraries(esp_test PRIVATE glfw OpenGL::GL)
endif()
