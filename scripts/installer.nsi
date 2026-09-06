; BPLC STA Monitor 安装器(NSIS 3.x)
; 特性:关闭运行中的程序 → 覆盖安装 → 开始菜单/桌面快捷方式 → 卸载器
; 用法:makensis /DVERSION=1.0.1 /DSRC=release\app installer.nsi
;       产物 BPLC_STA_Monitor_Setup_v<VERSION>.exe

!include "MUI2.nsh"
!include "FileFunc.nsh"

!ifndef VERSION
  !define VERSION "1.0.1"
!endif
!ifndef SRC
  !define SRC "..\release"
!endif

Name "BPLC STA Monitor v${VERSION}"
OutFile "..\dist\BPLC_STA_Monitor_Setup_v${VERSION}.exe"
; per-user 安装:无需管理员,UAC 不打扰,静默升级(/S)可全自动
InstallDir "$LOCALAPPDATA\Programs\BPLC_STA_Monitor"
InstallDirRegKey HKCU "Software\BPLC_STA_Monitor" "InstallDir"
RequestExecutionLevel user
Unicode true
SetCompressor /SOLID lzma
CRCCheck on
XPStyle on

!define MUI_ICON "..\icons\app.ico"
!define MUI_UNICON "..\icons\app.ico"
!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"

; 卸载运行中的程序(先尝试优雅退出;失败强制结束)
Function .onInit
  nsExec::ExecToStack 'tasklist /FI "IMAGENAME eq BPLC_STA_Monitor.exe" /NH'
  Pop $0
  Pop $1
  ${If} $1 != ""
    nsExec::Exec 'taskkill /IM BPLC_STA_Monitor.exe /F'
    Sleep 500
  ${EndIf}
FunctionEnd

Section "主程序" SEC_MAIN
  SetOutPath "$INSTDIR"
  ; 升级时先清理旧版可能遗留的文件(.o/.obj 等编译产物与旧 DLL 一律不带入)
  RMDir /r "$INSTDIR\platforms"
  RMDir /r "$INSTDIR\styles"
  RMDir /r "$INSTDIR\tls"
  RMDir /r "$INSTDIR\iconengines"
  RMDir /r "$INSTDIR\imageformats"
  ; 清旧版残留 DLL(升级时旧 DLL 可能多余;先删后写保证新包干净)
  File /r /x "*.o" /x "*.obj" /x "*.res" /x "Makefile*" /x ".qmake.stash" "${SRC}\*.*"
  WriteRegStr HKCU "Software\BPLC_STA_Monitor" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BPLC_STA_Monitor" \
    "DisplayName" "BPLC STA Monitor"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BPLC_STA_Monitor" \
    "DisplayIcon" "$INSTDIR\BPLC_STA_Monitor.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BPLC_STA_Monitor" \
    "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BPLC_STA_Monitor" \
    "Publisher" "aron566"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BPLC_STA_Monitor" \
    "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteUninstaller "$INSTDIR\uninstall.exe"

  CreateDirectory "$SMPROGRAMS\BPLC STA Monitor"
  CreateShortcut "$SMPROGRAMS\BPLC STA Monitor\BPLC STA Monitor.lnk" \
    "$INSTDIR\BPLC_STA_Monitor.exe" "" "$INSTDIR\BPLC_STA_Monitor.exe" 0
  CreateShortcut "$SMPROGRAMS\BPLC STA Monitor\卸载.lnk" \
    "$INSTDIR\uninstall.exe"
  CreateShortcut "$DESKTOP\BPLC STA Monitor.lnk" \
    "$INSTDIR\BPLC_STA_Monitor.exe" "" "$INSTDIR\BPLC_STA_Monitor.exe" 0
SectionEnd

Section "Uninstall"
  nsExec::Exec 'taskkill /IM BPLC_STA_Monitor.exe /F'
  Sleep 300
  Delete "$DESKTOP\BPLC STA Monitor.lnk"
  RMDir /r "$SMPROGRAMS\BPLC STA Monitor"
  Delete "$INSTDIR\uninstall.exe"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BPLC_STA_Monitor"
  DeleteRegKey HKCU "Software\BPLC_STA_Monitor"
SectionEnd
