; Loopwright for Windows: one installer (NSIS, free for commercial use) that puts the VST3 and CLAP plugins
; where every DAW looks, installs the app with a Start menu shortcut, and adds an uninstaller.
; Built in CI: makensis /DAPPVERSION=0.9.x /DSRC=<folder with the built files> /DOUTDIR=<folder> loopwright.nsi
!ifndef APPVERSION
  !define APPVERSION "0.9.0"
!endif
!ifndef SRC
  !define SRC "..\..\dist"
!endif
!ifndef OUTDIR
  !define OUTDIR "."
!endif

Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!include "Sections.nsh"

Name "Loopwright"
OutFile "${OUTDIR}\Loopwright-Setup.exe"
InstallDir "$PROGRAMFILES64\Loopwright"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
BrandingText "loop-wright.com"
VIProductVersion "${APPVERSION}.0"
VIAddVersionKey "ProductName" "Loopwright"
VIAddVersionKey "CompanyName" "Loopwright"
VIAddVersionKey "FileDescription" "Loopwright installer"
VIAddVersionKey "FileVersion" "${APPVERSION}"
VIAddVersionKey "ProductVersion" "${APPVERSION}"
VIAddVersionKey "LegalCopyright" "Loopwright"

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\Loopwright.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Open Loopwright now"
!define MUI_FINISHPAGE_TEXT "Loopwright is installed.$\r$\n$\r$\nIn your DAW, rescan plugins and load Loopwright as an instrument (VST3 or CLAP). Or open the Loopwright app from the Start menu."
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "Loopwright needs 64-bit Windows 10 or later."
    Abort
  ${EndIf}
  SetRegView 64
FunctionEnd

Section "Loopwright app" SecApp
  SetOutPath "$INSTDIR"
  File "${SRC}\Loopwright.exe"
  CreateDirectory "$SMPROGRAMS\Loopwright"
  CreateShortcut "$SMPROGRAMS\Loopwright\Loopwright.lnk" "$INSTDIR\Loopwright.exe"
  CreateShortcut "$SMPROGRAMS\Loopwright\Uninstall Loopwright.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "VST3 plugin (Ableton, FL Studio, Cubase, Studio One, Reaper...)" SecVst3
  SetOutPath "$COMMONFILES64\VST3\Loopwright.vst3"
  File /r "${SRC}\Loopwright.vst3\*"
SectionEnd

Section "CLAP plugin (Bitwig, Reaper, FL Studio 2024+)" SecClap
  SetOutPath "$COMMONFILES64\CLAP"
  File "${SRC}\Loopwright.clap"
SectionEnd

Section "Desktop shortcut" SecDesk
  CreateShortcut "$DESKTOP\Loopwright.lnk" "$INSTDIR\Loopwright.exe"
SectionEnd

Section "-uninstaller"
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  !define UNKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Loopwright"
  WriteRegStr HKLM "${UNKEY}" "DisplayName" "Loopwright"
  WriteRegStr HKLM "${UNKEY}" "DisplayVersion" "${APPVERSION}"
  WriteRegStr HKLM "${UNKEY}" "Publisher" "Loopwright"
  WriteRegStr HKLM "${UNKEY}" "URLInfoAbout" "https://loop-wright.com"
  WriteRegStr HKLM "${UNKEY}" "DisplayIcon" "$INSTDIR\Loopwright.exe"
  WriteRegStr HKLM "${UNKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKLM "${UNKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNKEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  SetRegView 64
  Delete "$INSTDIR\Loopwright.exe"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  RMDir /r "$COMMONFILES64\VST3\Loopwright.vst3"
  Delete "$COMMONFILES64\CLAP\Loopwright.clap"
  Delete "$SMPROGRAMS\Loopwright\Loopwright.lnk"
  Delete "$SMPROGRAMS\Loopwright\Uninstall Loopwright.lnk"
  RMDir "$SMPROGRAMS\Loopwright"
  Delete "$DESKTOP\Loopwright.lnk"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Loopwright"
SectionEnd
