cmake_minimum_required(VERSION 3.21)

project(Hearthlight
    VERSION 0.1.0
    DESCRIPTION "Hearthlight - Your home for every world (Minecraft: Java Edition launcher)"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Options
option(HEARTHLIGHT_BUILD_TESTS "Build unit tests" ON)
option(HEARTHLIGHT_ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    Svg
    SvgWidgets
    Concurrent
)

if(HEARTHLIGHT_BUILD_TESTS)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    enable_testing()
endif()

if(APPLE)
    # Universal binary by default when building on macOS (can be overridden via preset).
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
    endif()
endif()

if(WIN32)
    add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_subdirectory(src/core)
add_subdirectory(src/ui)
add_subdirectory(src/app)

if(HEARTHLIGHT_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Translations: compile translations/*.ts into .qm with Qt Linguist when
# available (CI and full Qt installs). The .qm files land in
# <build>/translations/ and install next to the binary; at runtime the app
# loads :/i18n first, then <dataDir>/translations/hearthlight_<code>.qm, so
# dropping a .qm there is always enough. Missing tools = graceful skip.
find_package(Qt6 6.5 QUIET OPTIONAL_COMPONENTS LinguistTools)
file(GLOB HEARTHLIGHT_TS_FILES "${CMAKE_SOURCE_DIR}/translations/*.ts")
if(TARGET Qt6::lrelease AND HEARTHLIGHT_TS_FILES)
    set(HEARTHLIGHT_QM_FILES)
    foreach(ts ${HEARTHLIGHT_TS_FILES})
        get_filename_component(stem "${ts}" NAME_WE)
        set(qm "${CMAKE_BINARY_DIR}/translations/${stem}.qm")
        add_custom_command(
            OUTPUT "${qm}"
            COMMAND Qt6::lrelease "${ts}" -qm "${qm}"
            DEPENDS "${ts}"
            COMMENT "lrelease ${stem}"
            VERBATIM
        )
        list(APPEND HEARTHLIGHT_QM_FILES "${qm}")
    endforeach()
    add_custom_target(hearthlight_translations ALL DEPENDS ${HEARTHLIGHT_QM_FILES})
    install(FILES ${HEARTHLIGHT_QM_FILES} DESTINATION translations OPTIONAL)
endif()
