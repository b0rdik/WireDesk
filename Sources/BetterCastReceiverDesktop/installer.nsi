; WireDesk per-user installer. Derived from BetterCast; GPL-3.0 retained.
; Driver installation, firewall and network changes are never implicit.
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!ifndef PRODUCT_VERSION
 !error "PRODUCT_VERSION is required"
!endif
!ifndef PRODUCT_NUMERIC_VERSION
 !error "PRODUCT_NUMERIC_VERSION is required"
!endif
!define PRODUCT_NAME "WireDesk"
!define PRODUCT_EXE "WireDesk.exe"
!define REGKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\WireDesk"
Name "WireDesk ${PRODUCT_VERSION}"
OutFile "WireDesk-Setup-${PRODUCT_VERSION}.exe"
InstallDir "$LOCALAPPDATA\Programs\WireDesk"
InstallDirRegKey HKCU "${REGKEY}" "InstallLocation"
RequestExecutionLevel user
Unicode true
SetCompressor /SOLID lzma
VIProductVersion "${PRODUCT_NUMERIC_VERSION}"
VIAddVersionKey "ProductName" "WireDesk"
VIAddVersionKey "ProductVersion" "${PRODUCT_VERSION}"
VIAddVersionKey "FileVersion" "${PRODUCT_VERSION}"
VIAddVersionKey "CompanyName" "b0rdik"
VIAddVersionKey "FileDescription" "WireDesk Installer"
VIAddVersionKey "LegalCopyright" "Derived from BetterCast (Stephen Lovino); WireDesk contributors"
!define MUI_ICON "appicon.ico"
!define MUI_UNICON "appicon.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\WireDesk.exe"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Russian"
Function .onInit
 ${IfNot} ${RunningX64}
  MessageBox MB_ICONSTOP|MB_OK "WireDesk requires Windows x64."
  Abort
 ${EndIf}
 SetRegView 64
 ; A verified WinSparkle update has already requested graceful shutdown.
 ; Manual install also asks only WireDesk to close; never kills other applications.
 FindWindow $0 "" "WireDesk"
 ${If} $0 != 0
  SendMessage $0 16 0 0
  Sleep 1500
 ${EndIf}
FunctionEnd
Section "WireDesk"
 SetOutPath "$INSTDIR"
 File /r "artifact\*.*"
 WriteUninstaller "$INSTDIR\uninstall.exe"
 CreateDirectory "$SMPROGRAMS\WireDesk"
 CreateShortCut "$SMPROGRAMS\WireDesk\WireDesk.lnk" "$INSTDIR\WireDesk.exe"
 WriteRegStr HKCU "${REGKEY}" "DisplayName" "WireDesk"
 WriteRegStr HKCU "${REGKEY}" "DisplayVersion" "${PRODUCT_VERSION}"
 WriteRegStr HKCU "${REGKEY}" "Publisher" "b0rdik"
 WriteRegStr HKCU "${REGKEY}" "InstallLocation" "$INSTDIR"
 WriteRegStr HKCU "${REGKEY}" "DisplayIcon" "$INSTDIR\WireDesk.exe,0"
 WriteRegStr HKCU "${REGKEY}" "UninstallString" '$\"$INSTDIR\uninstall.exe$\"'
 WriteRegStr HKCU "${REGKEY}" "URLInfoAbout" "https://github.com/b0rdik/WireDesk"
 WriteRegDWORD HKCU "${REGKEY}" "NoModify" 1
 WriteRegDWORD HKCU "${REGKEY}" "NoRepair" 1
 WriteRegStr HKCU "Software\Classes\wiredesk" "" "URL:WireDesk pairing"
 WriteRegStr HKCU "Software\Classes\wiredesk" "URL Protocol" ""
 WriteRegStr HKCU "Software\Classes\wiredesk\shell\open\command" "" '$\"$INSTDIR\WireDesk.exe$\" --pair $\"%1$\"'
 WriteRegStr HKCU "Software\Classes\.wiredesk-pair" "" "WireDesk.Pairing"
 WriteRegStr HKCU "Software\Classes\WireDesk.Pairing\shell\open\command" "" '$\"$INSTDIR\WireDesk.exe$\" --pair-file $\"%1$\"'
SectionEnd
Section "Uninstall"
 DeleteRegKey HKCU "${REGKEY}"
 DeleteRegKey HKCU "Software\Classes\wiredesk"
 DeleteRegKey HKCU "Software\Classes\WireDesk.Pairing"
 DeleteRegKey HKCU "Software\Classes\.wiredesk-pair"
 RMDir /r "$SMPROGRAMS\WireDesk"
 RMDir /r "$INSTDIR"
 ; Keep the user's settings/pairing. Do not remove an independently installed VDD.
SectionEnd
