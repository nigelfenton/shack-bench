cmake_minimum_required(VERSION 3.20)
project(TciMonitor
    VERSION 0.1.0
    DESCRIPTION "Diagnostic monitor for the TCI WebSocket protocol"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS
    Core
    Gui
    Network
    Widgets
    WebSockets
)

set(TCIMON_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/TciClient.cpp
    src/TciClient.h
    src/SwrPlot.cpp
    src/SwrPlot.h
    src/TciCommands.cpp
    src/TciCommands.h
    src/CommandDescriptionDialog.cpp
    src/CommandDescriptionDialog.h
    src/CommandsReferenceDialog.cpp
    src/CommandsReferenceDialog.h
    src/MdnsBrowser.cpp
    src/MdnsBrowser.h
    src/DiscoveryDialog.cpp
    src/DiscoveryDialog.h
)

if(WIN32)
    add_executable(TciMonitor WIN32 ${TCIMON_SOURCES})
elseif(APPLE)
    add_executable(TciMonitor MACOSX_BUNDLE ${TCIMON_SOURCES})
else()
    add_executable(TciMonitor ${TCIMON_SOURCES})
endif()

target_include_directories(TciMonitor PRIVATE src)
target_link_libraries(TciMonitor PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Widgets
    Qt6::WebSockets
)

if(MSVC)
    target_compile_options(TciMonitor PRIVATE /W3 /permissive-)
    target_compile_definitions(TciMonitor PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(TciMonitor PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Windows: copy Qt DLLs + plugins next to the binary so it runs by
# double-click without Qt on PATH.
if(WIN32)
    get_target_property(_qt6_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt6_bin "${_qt6_qmake}" DIRECTORY)
    find_program(WINDEPLOYQT_EXE windeployqt HINTS "${_qt6_bin}")
    if(WINDEPLOYQT_EXE)
        add_custom_command(TARGET TciMonitor POST_BUILD
            COMMAND "${WINDEPLOYQT_EXE}"
                    $<IF:$<CONFIG:Debug>,--debug,--release>
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-quick-import
                    --compiler-runtime
                    "$<TARGET_FILE:TciMonitor>"
            COMMENT "Running windeployqt to bundle Qt runtime DLLs"
            VERBATIM
        )
    endif()
endif()

if(APPLE)
    set_target_properties(TciMonitor PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME           "TCI Monitor"
        MACOSX_BUNDLE_GUI_IDENTIFIER        "com.g0jkn.tcimonitor"
        MACOSX_BUNDLE_BUNDLE_VERSION        "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING  "${PROJECT_VERSION}"
    )
endif()
