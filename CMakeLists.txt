cmake_minimum_required(VERSION 3.20)
project(kwin-helper)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# A plain "cmake -B build" would otherwise build without optimisation
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/imgui")

# Tests only, without the overlay program, so no Wayland, EGL, GLFW or OpenGL packages are needed:
#   cmake -B cmake-build-tests -DKWINHELPER_TESTS_ONLY=ON
option(KWINHELPER_TESTS_ONLY "Build only the headless tests (no graphics packages needed)" OFF)
if(NOT KWINHELPER_TESTS_ONLY)
find_package(glfw3 REQUIRED)
find_package(OpenGL REQUIRED)

# The layer-shell overlay window: Wayland client + EGL, and the protocol code made from draw/wayland/*.xml
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client wayland-cursor wayland-egl egl)
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
        utils/itemvalues.cpp
        utils/itemvalues.h
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
# Offset-finding probes (KWINHELPER_PROBE=... at run time). Off in the release build so the shipped binary carries no
# probe code or probe strings. For a probe build: cmake -B cmake-build-probe -DKWINHELPER_PROBES=ON
option(KWINHELPER_PROBES "Compile the offset-finding probes into the program" OFF)

add_executable(kwin-helper
        main.cpp
        config.h
        memory/memory.h
        ${IMGUI_SOURCES}
)

# --- 3. INCLUDES AND LINKS ---
target_include_directories(kwin-helper PRIVATE
        ${GEN_DIR}
        ${CMAKE_SOURCE_DIR}
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)

target_link_libraries(kwin-helper PRIVATE glfw OpenGL::GL PkgConfig::WAYLAND)
if(KWINHELPER_PROBES)
    target_compile_definitions(kwin-helper PRIVATE KWINHELPER_PROBES)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(kwin-helper PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(kwin-helper PRIVATE -s -Wl,--gc-sections)
endif()
endif() # NOT KWINHELPER_TESTS_ONLY
# --- Headless tests (they need no game and no display): cmake -B cmake-build-tests -DKWINHELPER_TESTS=ON
option(KWINHELPER_TESTS "Build the headless tests" OFF)
if(KWINHELPER_TESTS OR KWINHELPER_TESTS_ONLY)
    enable_testing()
    set(TEST_HOME ${CMAKE_BINARY_DIR}/test-home) #every test runs with this HOME, never the real ~/.config/kwin-helper
    file(MAKE_DIRECTORY ${TEST_HOME})
    function(kwinhelper_test name)
        add_executable(${name} tests/${name}.cpp ${ARGN})
        target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR} ${IMGUI_DIR} ${IMGUI_DIR}/backends)
        target_compile_definitions(${name} PRIVATE KWINHELPER_PROBES) #the tests always exercise the probes
        add_test(NAME ${name} COMMAND ${name})
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "HOME=${TEST_HOME}")
    endfunction()
    kwinhelper_test(key_test)
    kwinhelper_test(scatter_test memory/memory.cpp)
    kwinhelper_test(draw_test)
    kwinhelper_test(settings_test)
    kwinhelper_test(offsets_test)
    kwinhelper_test(skeleton_test)
    kwinhelper_test(gamescope_test utils/gamescope.cpp)
    kwinhelper_test(gamestate_test gamestate/GameState.cpp gamestate/Fast.cpp gamestate/Tracker.cpp gamestate/Probe.cpp utils/gamescope.cpp
            utils/names.cpp utils/labels.cpp utils/labeler.cpp memory/memory.cpp)
    target_link_libraries(gamestate_test PRIVATE pthread)
    kwinhelper_test(names_test utils/names.cpp gamestate/Probe.cpp memory/memory.cpp)
    kwinhelper_test(labels_test utils/names.cpp utils/labels.cpp utils/labeler.cpp memory/memory.cpp)
    kwinhelper_test(itemvalues_test utils/itemvalues.cpp)
    kwinhelper_test(amp_test)
    kwinhelper_test(marks_test)
    kwinhelper_test(fuzz_test utils/names.cpp utils/labels.cpp utils/labeler.cpp memory/memory.cpp)
    kwinhelper_test(menu_test utils/labels.cpp utils/itemvalues.cpp
            ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp)
    kwinhelper_test(esp_test utils/labels.cpp utils/itemvalues.cpp
            ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp)
endif()
