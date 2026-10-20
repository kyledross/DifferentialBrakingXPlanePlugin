cmake_minimum_required(VERSION 3.16)
project(DiffBrakePlugin)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(DIFF_BRAKE_PLUGIN_VERSION "development" CACHE STRING
        "Version displayed in X-Plane plugin information")

find_package(OpenGL REQUIRED)

set(IMGUI_SOURCES
    src/imgui/imgui.cpp
    src/imgui/imgui_draw.cpp
    src/imgui/imgui_impl_opengl3.cpp
    src/imgui/imgui_tables.cpp
    src/imgui/imgui_widgets.cpp
)

set(PLUGIN_SOURCES
    src/DiffBrakePlugin.cpp
    src/SettingsManager.cpp
    src/SettingsUI.cpp
    ${IMGUI_SOURCES}
)

add_library(DiffBrakePlugin SHARED ${PLUGIN_SOURCES})

target_include_directories(DiffBrakePlugin PRIVATE
        src
        src/imgui
        src/json
        SDK/CHeaders/XPLM
        SDK/CHeaders/Widgets)

target_link_libraries(DiffBrakePlugin PRIVATE OpenGL::GL)

# Tell the X-Plane SDK headers which platform we are targeting and that we
# are compiling a plugin (not the XPLM library itself), plus which SDK
# feature levels we want to use.
target_compile_definitions(DiffBrakePlugin PRIVATE
        DIFF_BRAKE_PLUGIN_VERSION="${DIFF_BRAKE_PLUGIN_VERSION}"
        XPLM_API_VERSION=400
        XPLM200=1
        XPLM210=1
        XPLM300=1
        XPLM301=1
        XPLM400=1)

if (APPLE)
    target_compile_definitions(DiffBrakePlugin PRIVATE APL=1 IBM=0 LIN=0)
    target_link_options(DiffBrakePlugin PRIVATE -undefined dynamic_lookup)
elseif (WIN32)
    target_compile_definitions(DiffBrakePlugin PRIVATE APL=0 IBM=1 LIN=0)
else ()
    target_compile_definitions(DiffBrakePlugin PRIVATE APL=0 IBM=0 LIN=1)
endif ()

# X-Plane plugins are shared libraries with a ".xpl" extension and no "lib"
# prefix, following the platform-specific directory layout X-Plane expects:
#   <plugin name>/<platform>/<plugin name>.xpl
set_target_properties(DiffBrakePlugin PROPERTIES
        PREFIX ""
        SUFFIX ".xpl"
        OUTPUT_NAME "DiffBrakePlugin")
if (UNIX AND NOT APPLE)
    target_link_options(DiffBrakePlugin PRIVATE -static-libgcc -static-libstdc++)
endif ()

enable_testing()
add_executable(test_castering_detection
        tests/test_castering_detection.cpp
        src/SettingsManager.cpp
        src/SettingsUI.cpp
        ${IMGUI_SOURCES})
target_include_directories(test_castering_detection PRIVATE
        src
        src/imgui
        src/json
        SDK/CHeaders/XPLM
        SDK/CHeaders/Widgets)
target_link_libraries(test_castering_detection PRIVATE OpenGL::GL)
target_compile_definitions(test_castering_detection PRIVATE
        DIFF_BRAKE_PLUGIN_VERSION="${DIFF_BRAKE_PLUGIN_VERSION}"
        XPLM_API_VERSION=400
        XPLM200=1
        XPLM210=1
        XPLM300=1
        XPLM301=1
        XPLM400=1)
if (APPLE)
    target_compile_definitions(test_castering_detection PRIVATE APL=1 IBM=0 LIN=0)
elseif (WIN32)
    target_compile_definitions(test_castering_detection PRIVATE APL=0 IBM=1 LIN=0)
else ()
    target_compile_definitions(test_castering_detection PRIVATE APL=0 IBM=0 LIN=1)
endif ()
add_test(NAME CasteringDetectionTest COMMAND test_castering_detection)
