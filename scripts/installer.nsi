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
  ; 注意:绝不能删 "$INSTDIR\plugins" —— 那是插件包总目录,用户从市场安装的
  ; 插件须跨升级保留(加载时按 ABI 过滤,不兼容的自然拒绝,不崩溃)。
  RMDir /r "$INSTDIR\platforms"
  RMDir /r "$INSTDIR\styles"
  RMDir /r "$INSTDIR\tls"
  RMDir /r "$INSTDIR\iconengines"
  RMDir /r "$INSTDIR\imageformats"
  ; 升级保留用户配置文件:即便包内带 config.ini 也不覆盖已存在的配置
  ; (全新安装目录无文件时正常写入默认)
  SetOverwrite off
  File /nonfatal "${SRC}\config.ini"
  SetOverwrite on
  ; 清旧版残留 DLL(升级时旧 DLL 可能多余;先删后写保证新包干净)
  ; 注意: "${SRC}\*.*" 这类通配对**非 ASCII 文件名会漏项** —— 实测中文名 PDF
  ; (BPLC_STA_Monitor使用说明书.pdf / Wireshark插件使用说明.pdf)根本没进包,
  ; 中文用户装完看不到中文使用手册。故这里 /x "*.pdf" 排除通配, 再显式列出 4 份。
  ; 本脚本为 UTF-8 with BOM, NSIS 3 Unicode 模式支持中文路径。
  ; 另注: NSIS 的 File 指令**不支持 "source destination" 双参数**(第二个参数会被当 source),
  ; 且省略 destination 时 destination = source 的**文件名**(不含目录) —— 会把 PDF 装到安装根目录。
  ; 故用 /oname=<目标路径> 单文件形式(注意 /oname 后的值**不能加引号**, 否则引号会被当名字的一部分)。
  ; 纵深防御: 打包脚本已按**白名单**只拷 12 个文件(见 scripts/package.sh / package_ci.sh),
  ; 但万一有人把拷贝逻辑改回 cp -r 整目录, 这里的 /x 是最后一道闸 —— 开发期回归样本与调试产物
  ; (pcap/bin/txt/生成器/DESIGN.md/test_2026)绝不能进安装包。
  ; (NSIS 安装器自身的 $PLUGINSDIR/ 资源不在 ${SRC} 下, 不会被这里影响)
  File /r /x "*.o" /x "*.obj" /x "*.res" /x "*.cpp" /x "*.h" /x "mocinclude.opt" /x "object_script*" /x "Makefile*" /x ".qmake.stash" /x "*.pdf" /x "*.pcap" /x "*.pcapng" /x "*.bin" /x "*.txt" /x "gen_*.py" /x "DESIGN.md" /x "test_2026" "${SRC}\*.*"
  File /oname=docs\BPLC_STA_Monitor使用说明书.pdf "${SRC}\docs\BPLC_STA_Monitor使用说明书.pdf"
  File /oname=docs\BPLC_STA_Monitor_User_Manual_EN.pdf "${SRC}\docs\BPLC_STA_Monitor_User_Manual_EN.pdf"
  File /oname=wireshark_support_plugins\Wireshark插件使用说明.pdf "${SRC}\wireshark_support_plugins\Wireshark插件使用说明.pdf"
  File /oname=wireshark_support_plugins\Wireshark_Plugin_Manual_EN.pdf "${SRC}\wireshark_support_plugins\Wireshark_Plugin_Manual_EN.pdf"
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
