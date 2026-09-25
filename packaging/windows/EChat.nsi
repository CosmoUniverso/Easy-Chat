Unicode True
!include "MUI2.nsh"

!define APPNAME "EChat"
!define COMPANY "Easy Chat"
!define VERSION "0.7.0"

Name "${APPNAME} ${VERSION}"
OutFile "EChat-Windows-x64-Setup.exe"
InstallDir "$LOCALAPPDATA\\Programs\\EChat"
RequestExecutionLevel user
SetCompressor /SOLID lzma

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_LANGUAGE "Italian"
!insertmacro MUI_LANGUAGE "English"

Section "EChat" SEC_MAIN
  SetOutPath "$INSTDIR"
  File /r "..\\..\\dist\\EChat\\*.*"
  CreateDirectory "$SMPROGRAMS\\EChat"
  CreateShortcut "$SMPROGRAMS\\EChat\\EChat.lnk" "$INSTDIR\\EChat.exe"
  CreateShortcut "$SMPROGRAMS\\EChat\\EChat Light.lnk" "$INSTDIR\\EChat.exe" "--light"
  CreateShortcut "$SMPROGRAMS\\EChat\\EChat Full.lnk" "$INSTDIR\\EChat.exe" "--full"
  CreateShortcut "$DESKTOP\\EChat.lnk" "$INSTDIR\\EChat.exe"
  WriteUninstaller "$INSTDIR\\Uninstall.exe"
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\\EChat.lnk"
  Delete "$SMPROGRAMS\\EChat\\EChat.lnk"
  Delete "$SMPROGRAMS\\EChat\\EChat Light.lnk"
  Delete "$SMPROGRAMS\\EChat\\EChat Full.lnk"
  RMDir "$SMPROGRAMS\\EChat"
  RMDir /r "$INSTDIR"
SectionEnd
