cmake_minimum_required(VERSION 3.16)
project(OpenTax VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OPENTAX_BUILD_TESTS "Build the OpenTax test suite" ON)
option(OPENTAX_BUILD_GUI "Build the OpenTax desktop app (Dear ImGui)" ON)

# Warnings for OpenTax's own code (not applied to third-party sources).
add_library(opentax_warnings INTERFACE)
if(MSVC)
    target_compile_options(opentax_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(opentax_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

# ------------------------------------------------------------------ cryptography
# Monocypher 4.0.3 (vendored unmodified; see SECURITY.md): Argon2id and XChaCha20-Poly1305
# for password-protected returns.
add_library(monocypher STATIC third_party/monocypher/monocypher.c)
target_include_directories(monocypher PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/monocypher)
set_target_properties(monocypher PROPERTIES C_STANDARD 99)

# ------------------------------------------------------------------ core engine
add_library(opentax_core
    src/util.cpp
    src/money.cpp
    src/date.cpp
    src/model.cpp
    src/rules.cpp
    src/rules_2025.cpp
    src/rules_2026.cpp
    src/calc.cpp
    src/crypto.cpp
    src/pdf.cpp
    src/return_pdf.cpp
    src/report.cpp
    src/cli.cpp
)
target_include_directories(opentax_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(opentax_core PRIVATE opentax_warnings monocypher)
if(WIN32)
    target_link_libraries(opentax_core PUBLIC bcrypt)  # BCryptGenRandom
endif()
if(MSVC)
    target_compile_options(opentax_core PUBLIC /utf-8 /bigobj)
    target_compile_definitions(opentax_core PUBLIC _CRT_SECURE_NO_WARNINGS)
endif()

# Self-contained executables on MinGW (no libstdc++/libgcc DLLs to ship).
function(opentax_static_runtime target)
    if(MINGW)
        target_link_options(${target} PRIVATE -static)
    endif()
endfunction()

# -------------------------------------------------------------------------- CLI
add_executable(opentax src/main.cpp)
target_link_libraries(opentax PRIVATE opentax_core opentax_warnings)
if(WIN32)
    target_link_libraries(opentax PRIVATE shell32)
endif()
opentax_static_runtime(opentax)
install(TARGETS opentax RUNTIME DESTINATION bin)

# ------------------------------------------------------------------------ tests
if(OPENTAX_BUILD_TESTS)
    enable_testing()
    add_executable(opentax_tests tests/test_main.cpp)
    target_link_libraries(opentax_tests PRIVATE opentax_core opentax_warnings monocypher)  # monocypher: Argon2 test vector
    target_compile_definitions(opentax_tests PRIVATE OPENTAX_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    opentax_static_runtime(opentax_tests)
    add_test(NAME opentax_tests COMMAND opentax_tests)
endif()

# -------------------------------------------------------------------------- GUI
if(OPENTAX_BUILD_GUI)
    set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
    add_library(imgui STATIC
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/misc/imgui_stdlib.cpp
    )
    target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends ${IMGUI_DIR}/misc)
    # Hardening: remove ImGui's built-in "open URL/file in shell" handler (ShellExecute / system())
    # so no widget can ever launch a URL or command, and drop deprecated APIs.
    target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

    set(OPENTAX_GUI_SOURCES
        gui/app.cpp
        gui/app_screens.cpp
        gui/app_review.cpp
        gui/widgets.cpp
        gui/theme.cpp
    )

    if(WIN32)
        target_sources(imgui PRIVATE ${IMGUI_DIR}/backends/imgui_impl_win32.cpp ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
        target_link_libraries(imgui PUBLIC d3d11 d3dcompiler dxgi dwmapi)
        add_executable(opentax-gui WIN32 ${OPENTAX_GUI_SOURCES} gui/main_win32_dx11.cpp gui/platform_win32.cpp)
        target_link_libraries(opentax-gui PRIVATE comdlg32 shell32 ole32)
    else()
        find_package(glfw3 3.3 REQUIRED)
        find_package(OpenGL REQUIRED)
        target_sources(imgui PRIVATE ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
        target_link_libraries(imgui PUBLIC glfw OpenGL::GL ${CMAKE_DL_LIBS})
        add_executable(opentax-gui ${OPENTAX_GUI_SOURCES} gui/main_glfw_opengl3.cpp gui/platform_posix.cpp)
    endif()
    target_link_libraries(opentax-gui PRIVATE opentax_core imgui opentax_warnings)
    opentax_static_runtime(opentax-gui)
    install(TARGETS opentax-gui RUNTIME DESTINATION bin)
endif()
