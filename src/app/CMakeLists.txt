cmake_minimum_required(VERSION 3.21)

add_library(HearthlightApp STATIC
    Application.h Application.cpp
)

target_include_directories(HearthlightApp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(HearthlightApp PUBLIC HearthlightUi HearthlightCore Qt6::Core Qt6::Widgets)
target_compile_features(HearthlightApp PUBLIC cxx_std_20)

add_executable(Hearthlight main.cpp)
target_link_libraries(Hearthlight PRIVATE HearthlightApp)

set_target_properties(Hearthlight PROPERTIES
    OUTPUT_NAME Hearthlight
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER "example.hearthlight.launcher"
    MACOSX_BUNDLE_BUNDLE_NAME "Hearthlight"
)

include(GNUInstallDirs)
install(TARGETS Hearthlight
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES ../../LICENSE ../../README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
if(UNIX AND NOT APPLE)
    install(FILES ../../packaging/linux/hearthlight.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
endif()
