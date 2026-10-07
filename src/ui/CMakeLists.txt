cmake_minimum_required(VERSION 3.21)

add_library(HearthlightUi STATIC
    Theme.h Theme.cpp
    IconProvider.h IconProvider.cpp
    MainWindow.h MainWindow.cpp
    PlaceholderPages.h PlaceholderPages.cpp
    VersionsPage.h VersionsPage.cpp
    ProfilesPage.h ProfilesPage.cpp
    HearthPage.h HearthPage.cpp
    DiscoverPage.h DiscoverPage.cpp
    GameLogDialog.h GameLogDialog.cpp
    AccountsPage.h AccountsPage.cpp
    SettingsPage.h SettingsPage.cpp
    LogViewerDialog.h LogViewerDialog.cpp
    widgets/PlayBar.h widgets/PlayBar.cpp
    widgets/AccountSwitcher.h widgets/AccountSwitcher.cpp
    dialogs/CreateOfflineAccountDialog.h dialogs/CreateOfflineAccountDialog.cpp
    dialogs/MicrosoftLoginDialog.h dialogs/MicrosoftLoginDialog.cpp
    dialogs/InstanceWizard.h dialogs/InstanceWizard.cpp
    dialogs/InstanceSettingsDialog.h dialogs/InstanceSettingsDialog.cpp
    dialogs/TaskProgressDialog.h dialogs/TaskProgressDialog.cpp
    dialogs/InstalledModsDialog.h dialogs/InstalledModsDialog.cpp
    dialogs/WorldsDialog.h dialogs/WorldsDialog.cpp
    dialogs/KindlingDialog.h dialogs/KindlingDialog.cpp
    resources/icons.qrc
)

target_include_directories(HearthlightUi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(HearthlightUi PUBLIC HearthlightCore Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::SvgWidgets)
target_compile_features(HearthlightUi PUBLIC cxx_std_20)
