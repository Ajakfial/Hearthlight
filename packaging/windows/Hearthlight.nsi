; Hearthlight Windows installer (NSIS).
; Builds Hearthlight-Setup-<version>.exe. Stage first, then compile:
;
;   1. cmake --preset windows-msvc
;   2. cmake --build --preset windows-msvc --config Release
;   3. mkdir stage && copy build\windows-msvc\src\app\Release\Hearthlight.exe stage\
;   4. windeployqt --release --no-translations --no-system-d3d-compiler stage\Hearthlight.exe
;   5. copy LICENSE README.md stage\
;   6. makensis /DVERSION=0.1.0 /DSTAGE=stage packaging\windows\Hearthlight.nsi
;
; Portable zip: zip the same staged folder plus an empty portable.txt next to
; Hearthlight.exe (the launcher then keeps everything in
; <exe-dir>/HearthlightData and writes nothing to AppData).

!include "MUI2.nsh"
!include "LogicLib.nsh"

Name "Hearthlight"
OutFile "Hearthlight-Setup-${VERSION}.exe"
InstallDir "$PROGRAMFILES64\Hearthlight"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

!ifndef STAGE
!define STAGE "stage"
!endif
!define APP_EXE "Hearthlight.exe"

!insertmacro MUI_PAGE_LICENSE "..\..\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "Hearthlight (required)" SecApp
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Hearthlight" \
    "DisplayName" "Hearthlight"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Hearthlight" \
    "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Hearthlight" \
    "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Hearthlight" \
    "Publisher" "Hearthlight contributors"
  CreateDirectory "$SMPROGRAMS\Hearthlight"
  CreateShortcut "$SMPROGRAMS\Hearthlight\Hearthlight.lnk" "$INSTDIR\${APP_EXE}"
  CreateShortcut "$SMPROGRAMS\Hearthlight\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Desktop shortcut" SecDesktop
  CreateShortcut "$DESKTOP\Hearthlight.lnk" "$INSTDIR\${APP_EXE}"
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\Hearthlight.lnk"
  RMDir /r "$SMPROGRAMS\Hearthlight"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Hearthlight"
  RMDir /r "$INSTDIR"
SectionEnd
