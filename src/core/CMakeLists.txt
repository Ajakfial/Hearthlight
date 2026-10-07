cmake_minimum_required(VERSION 3.21)

# miniz: single-file zip handling for Minecraft natives, loader installers
# and pack import/export. Downloaded once at configure time into the build
# dir; no system dependency, no git required.
set(MINIZ_URL https://github.com/richgel999/miniz/archive/refs/tags/3.0.2.zip)
set(MINIZ_SRC ${CMAKE_BINARY_DIR}/_deps/miniz-src)
if(NOT EXISTS ${MINIZ_SRC}/miniz.c)
    message(STATUS "Downloading miniz for zip extraction...")
    file(DOWNLOAD ${MINIZ_URL} ${CMAKE_BINARY_DIR}/_deps/miniz.zip SHOW_PROGRESS)
    file(ARCHIVE_EXTRACT INPUT ${CMAKE_BINARY_DIR}/_deps/miniz.zip DESTINATION ${CMAKE_BINARY_DIR}/_deps/miniz-tmp)
    file(GLOB MINIZ_UNPACKED ${CMAKE_BINARY_DIR}/_deps/miniz-tmp/miniz-*)
    list(GET MINIZ_UNPACKED 0 MINIZ_DIR)
    file(COPY ${MINIZ_DIR}/ DESTINATION ${MINIZ_SRC})
endif()
# miniz 3.x expects a generated miniz_export.h (shared-lib macros). We build
# static, so an empty stub is correct and keeps the one-file dependency.
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/_deps/miniz-gen)
file(WRITE ${CMAKE_BINARY_DIR}/_deps/miniz-gen/miniz_export.h
    "#pragma once\n#ifndef MINIZ_EXPORT\n#define MINIZ_EXPORT\n#endif\n")

add_library(HearthlightCore STATIC
    Constants.cpp
    AppSettings.h AppSettings.cpp
    Logger.h Logger.cpp
    NetworkStatus.h NetworkStatus.cpp
    Task.h Task.cpp
    DownloadManager.h DownloadManager.cpp
    Account.h Account.cpp
    SecureTokenStore.h SecureTokenStore.cpp
    MicrosoftAuth.h MicrosoftAuth.cpp
    AccountProvider.h AccountProvider.cpp
    AccountStore.h AccountStore.cpp
    GamePaths.h GamePaths.cpp
    VersionModel.h VersionModel.cpp
    MojangApi.h MojangApi.cpp
    ZipUtil.h ZipUtil.cpp
    VersionInstaller.h VersionInstaller.cpp
    JavaManager.h JavaManager.cpp
    Launcher.h Launcher.cpp
    Instance.h Instance.cpp
    InstanceManager.h InstanceManager.cpp
    ModLoader.h ModLoader.cpp
    ModrinthApi.h ModrinthApi.cpp
    ModManager.h ModManager.cpp
    Embers.h Embers.cpp
    Hearthstones.h Hearthstones.cpp
    CrashDoctor.h CrashDoctor.cpp
    Kindling.h Kindling.cpp
    Markdown.h Markdown.cpp
    # miniz is compiled straight into Core: one static lib, no transitive
    # link edges for tests/app/packaging to get wrong.
    # (miniz 3.x ships 4 translation units; the zip reader lives in miniz_zip.c)
    ${MINIZ_SRC}/miniz.c
    ${MINIZ_SRC}/miniz_tdef.c
    ${MINIZ_SRC}/miniz_tinfl.c
    ${MINIZ_SRC}/miniz_zip.c
)

target_include_directories(HearthlightCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_include_directories(HearthlightCore PRIVATE ${MINIZ_SRC} ${CMAKE_BINARY_DIR}/_deps/miniz-gen)
target_link_libraries(HearthlightCore PUBLIC Qt6::Core Qt6::Network Qt6::Concurrent)
if(WIN32)
    # Windows Credential Manager (SecureTokenStore).
    target_link_libraries(HearthlightCore PUBLIC advapi32)
endif()
if(APPLE)
    # macOS Keychain (SecureTokenStore).
    target_link_libraries(HearthlightCore PUBLIC "-framework Security" "-framework CoreFoundation")
endif()
target_compile_features(HearthlightCore PUBLIC cxx_std_20)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(${MINIZ_SRC}/miniz.c ${MINIZ_SRC}/miniz_tdef.c ${MINIZ_SRC}/miniz_tinfl.c
                                ${MINIZ_SRC}/miniz_zip.c
                                PROPERTIES COMPILE_OPTIONS "-Wno-conversion;-Wno-sign-conversion;-Wno-unused-parameter")
endif()
