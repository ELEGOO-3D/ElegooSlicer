; 该脚本使用 HM VNISEdit 脚本编辑器向导产生
!define LIBRARY_X64

; request admin rights for all users installation
RequestExecutionLevel admin

; 安装程序初始定义常量
!define PRODUCT_NAME "ElegooSlicer"
!define PRODUCT_PUBLISHER "Shenzhen Elegoo Technology Co.,Ltd"
;!define PRODUCT_WEB_SITE "https://www.elegoo.com"
!define PRODUCT_DIR_REGKEY "Software\Microsoft\Windows\CurrentVersion\App Paths\elegoo-slicer.exe"
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
!define PRODUCT_UNINST_ROOT_KEY "HKLM"
!define PRODUCT_STARTMENU_REGVAL "NSIS:StartMenuDir"


!define PRODUCT_UNINST_KEY_32 "Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"

SetCompressor lzma

VIProductVersion "${PRODUCT_VERSION}"
VIAddVersionKey "ProductName" "ElegooSlicer"
VIAddVersionKey "FileVersion" "${PRODUCT_VERSION}"
VIAddVersionKey "ProductVersion" "${PRODUCT_VERSION}"
VIAddVersionKey "CompanyName" "Shenzhen Elegoo Technology Co., Ltd" 
VIAddVersionKey "FileDescription" "ElegooSlicer"
VIAddVersionKey "LegalCopyright" ""

; ------ MUI 现代界面定义 (1.67 版本以上兼容) ------
!include "MUI.nsh"
!include "nsProcess.nsh"
!include "WordFunc.nsh"

; MUI 预定义常量
!define MUI_ABORTWARNING
!define MUI_ICON ".\resources\images\ElegooSlicer.ico"
!define MUI_UNICON ".\resources\images\ElegooSlicer.ico"

; 语言选择窗口常量设置
!define MUI_LANGDLL_REGISTRY_ROOT "${PRODUCT_UNINST_ROOT_KEY}"
!define MUI_LANGDLL_REGISTRY_KEY "${PRODUCT_UNINST_KEY}"
!define MUI_LANGDLL_REGISTRY_VALUENAME "NSIS:Language"

; 欢迎页面
!insertmacro MUI_PAGE_WELCOME
; 许可协议页面
!define MUI_LICENSEPAGE_CHECKBOX
!insertmacro MUI_PAGE_LICENSE ".\LICENSE.txt"
; 安装目录选择页面
!insertmacro MUI_PAGE_DIRECTORY
; 开始菜单设置页面
var ICONS_GROUP
;!define MUI_STARTMENUPAGE_NODISABLE
!define MUI_STARTMENUPAGE_DEFAULTFOLDER "ElegooSlicer"
!define MUI_STARTMENUPAGE_REGISTRY_ROOT "${PRODUCT_UNINST_ROOT_KEY}"
!define MUI_STARTMENUPAGE_REGISTRY_KEY "${PRODUCT_UNINST_KEY}"
!define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "${PRODUCT_STARTMENU_REGVAL}"

; 安装界面包含的语言设置
!define LANG_ENGLISH 1033
!define LANG_CHINESE_SIMPLIFIED 2052

LangString MUI_STARTMENUPAGE_TEXT ${LANG_ENGLISH} "Do not create shortcuts"
LangString MUI_STARTMENUPAGE_TEXT ${LANG_CHINESE_SIMPLIFIED} "不创建快捷方式"

!define MUI_STARTMENUPAGE_TEXT_CHECKBOX $(MUI_STARTMENUPAGE_TEXT)
!insertmacro MUI_PAGE_STARTMENU Application $ICONS_GROUP
; 安装过程页面
!insertmacro MUI_PAGE_INSTFILES
; 安装完成页面
!define MUI_FINISHPAGE_RUN "$INSTDIR\elegoo-slicer.exe"
; !define MUI_FINISHPAGE_RUN
; !define MUI_FINISHPAGE_RUN_FUNCTION "RunMainApp"
!insertmacro MUI_PAGE_FINISH

; 安装卸载过程页面
!insertmacro MUI_UNPAGE_INSTFILES


!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "SimpChinese"


; 安装预释放文件
!insertmacro MUI_RESERVEFILE_LANGDLL
!insertmacro MUI_RESERVEFILE_INSTALLOPTIONS
; ------ MUI 现代界面定义结束 ------

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile ".\build\ElegooSlicer_Windows_Installer_V${PRODUCT_VERSION}.exe"
InstallDir "$PROGRAMFILES64\ElegooSlicer"
InstallDirRegKey HKLM "${PRODUCT_UNINST_KEY}" "UninstallString"
ShowInstDetails show
ShowUnInstDetails show

Section "MainSection" SEC01
  ; set shell context to all users
  SetShellVarContext all

  ${WordFind} "$INSTDIR" "ElegooSlicer" "E+1{" $R0
  IfErrors notfound end
  notfound:
	StrCpy $INSTDIR $INSTDIR\ElegooSlicer	
  end:

  SetOutPath "$INSTDIR"
  SetOverwrite ifnewer
  
  File /r "${INSTALL_PATH}\*.*"
  
  ; 创建开始菜单快捷方式
  !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
  CreateDirectory "$SMPROGRAMS\$ICONS_GROUP"
  CreateShortCut "$SMPROGRAMS\$ICONS_GROUP\ElegooSlicer.lnk" "$INSTDIR\elegoo-slicer.exe" "" "$INSTDIR\resources\images\ElegooSlicer.ico" 0
  CreateShortCut "$DESKTOP\ElegooSlicer.lnk" "$INSTDIR\elegoo-slicer.exe" "" "$INSTDIR\resources\images\ElegooSlicer.ico" 0
  !insertmacro MUI_STARTMENU_WRITE_END
SectionEnd

Section -AdditionalIcons
  !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
  ;WriteIniStr "$INSTDIR\${PRODUCT_NAME}.url" "InternetShortcut" "URL" "${PRODUCT_WEB_SITE}"
  ;CreateShortCut "$SMPROGRAMS\$ICONS_GROUP\Website.lnk" "$INSTDIR\${PRODUCT_NAME}.url"
  CreateShortCut "$SMPROGRAMS\$ICONS_GROUP\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
  !insertmacro MUI_STARTMENU_WRITE_END
SectionEnd

Section -Post
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "${PRODUCT_DIR_REGKEY}" "" "$INSTDIR\elegoo-slicer.exe"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayName" "$(^Name)"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayIcon" "$INSTDIR\elegoo-slicer.exe"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  ;WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "URLInfoAbout" "${PRODUCT_WEB_SITE}"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
SectionEnd


#-- 根据 NSIS 脚本编辑规则，所有 Function 区段必须放置在 Section 区段之后编写，以避免安装程序出现未可预知的问题。--#
Var UNINSTALL_PROG
Var OLD_VER
Var UNINSTALL_PROG_32
Var OLD_VER_32
Var PUBLIC_DESKTOP_PATH
Var OLD_PATH
Var VER_INFO
Var UNINSTALL_PATH

; 定义语言字符串
LangString TXT_UNINSTALL_SUCCESS ${LANG_ENGLISH} "$(^Name) has been successfully removed from your computer."
LangString TXT_UNINSTALL_SUCCESS ${LANG_CHINESE_SIMPLIFIED} "$(^Name) 已成功从您的计算机中删除。"


Function UninstallOldVersion
  ; 执行卸载 	
  Exch $0 
  ${If} $0 != ""
    ${WordReplace} "$0" "$\"" "" "+" $0
    StrCpy $UNINSTALL_PATH $0
	StrCpy $OLD_PATH $0 -13	
	ExecWait '"$UNINSTALL_PATH" /S _?=$OLD_PATH' $0
	DetailPrint "Uninstall.exe returned $0"
	Delete "$UNINSTALL_PATH"
	RMDir /r $OLD_PATH
	;旧版本桌面快捷方式放在了public下 默认的卸载程序无法删除掉
	ReadRegStr $PUBLIC_DESKTOP_PATH HKLM "Software\Microsoft\Windows\CurrentVersion\Explorer\Shell Folders" "Common Desktop"
    Delete "$PUBLIC_DESKTOP_PATH\ElegooSlicer.lnk"
  ${EndIf}
FunctionEnd


Function .onInit
  ; set shell context to all users
  SetShellVarContext all
  SetRegView 64
  ;!insertmacro MUI_LANGDLL_DISPLAY 
  System::Call 'Kernel32::GetUserDefaultUILanguage() i.r0'
  ${If} $0 == ${LANG_CHINESE_SIMPLIFIED}
      StrCpy $LANGUAGE ${LANG_CHINESE_SIMPLIFIED}
  ${Else}
      StrCpy $LANGUAGE ${LANG_ENGLISH}
  ${EndIf}

  ReadRegStr $UNINSTALL_PROG ${PRODUCT_UNINST_ROOT_KEY} ${PRODUCT_UNINST_KEY} "UninstallString"
  ReadRegStr $OLD_VER ${PRODUCT_UNINST_ROOT_KEY} ${PRODUCT_UNINST_KEY} "DisplayVersion"
  ReadRegStr $UNINSTALL_PROG_32 ${PRODUCT_UNINST_ROOT_KEY} ${PRODUCT_UNINST_KEY_32} "UninstallString"
  ReadRegStr $OLD_VER_32 ${PRODUCT_UNINST_ROOT_KEY} ${PRODUCT_UNINST_KEY_32} "DisplayVersion"    

  nsProcess::_FindProcess "elegoo-slicer.exe"
  Pop $R0 
  ${If} $R0 == 0
    ;在安装和卸载的初始化中LangString还未根据语言类型加载，动态判断处理
	${If} $LANGUAGE == ${LANG_ENGLISH}
	  MessageBox MB_OKCANCEL|MB_ICONSTOP "The installer has detected that ${PRODUCT_NAME} is running.$\nClick 'OK' to force close ${PRODUCT_NAME} and continue the installation.Click 'Cancel' to exit the installer." /SD IDOK IDOK kill_and_continue IDCANCEL abort_install
	${Else}
	  MessageBox MB_OKCANCEL|MB_ICONSTOP "安装程序检测到 ${PRODUCT_NAME} 正在运行。是否要强制关闭它并继续安装?$\n点击 '确认' 强制关闭并继续安装，点击 '取消' 退出安装程序。" /SD IDOK IDOK kill_and_continue IDCANCEL abort_install
	${EndIf} 
  ${EndIf} 
  
  StrCpy $VER_INFO ""
  
  ${If} $UNINSTALL_PROG != ""
    StrCpy $VER_INFO $OLD_VER
  ${ElseIf} $UNINSTALL_PROG_32 != ""	
	StrCpy $VER_INFO $OLD_VER_32
  ${Else}
    goto done
  ${EndIf} 
   	
  ${If} $LANGUAGE == ${LANG_ENGLISH}
	MessageBox MB_YESNO|MB_ICONQUESTION "Detected version $VER_INFO. Do you want to uninstall it and continue with the installation?" /SD IDYES IDYES uninstall_old_version IDNO abort_install
  ${Else}
    MessageBox MB_YESNO|MB_ICONQUESTION "检测到版本 $VER_INFO。是否要卸载它并继续安装?" /SD IDYES IDYES uninstall_old_version IDNO abort_install
  ${EndIf}
  
  abort_install:
    Abort
	
  kill_and_continue:
    nsProcess::_KillProcess "elegoo-slicer.exe"
    Pop $R0
	Sleep 1000 
	goto uninstall_old_version 
	
  uninstall_old_version:  
    Push $UNINSTALL_PROG
    Call UninstallOldVersion
	Push $UNINSTALL_PROG_32
    Call UninstallOldVersion 
	${If} $OLD_PATH != ""
	  StrCpy $INSTDIR $OLD_PATH
	${EndIf}
	
  done:
	
FunctionEnd

Function RunMainApp
 ; 使用 System::Call 以普通权限运行程序
  System::Call 'shell32::ShellExecute(i 0, t"open", t"explorer.exe", t" /e,elegoo-slicer.exe", t"$INSTDIR\\", i 0)'
FunctionEnd

/******************************
 *  以下是安装程序的卸载部分  *
 ******************************/

Section Uninstall
  ; set shell context to all users for uninstallation
  SetShellVarContext all
  SetRegView 64
  !insertmacro MUI_STARTMENU_GETFOLDER "Application" $ICONS_GROUP
  Delete "$SMPROGRAMS\$ICONS_GROUP\Uninstall.lnk"
  Delete "$SMPROGRAMS\$ICONS_GROUP\Website.lnk"
  Delete "$DESKTOP\ElegooSlicer.lnk"
  Delete "$SMPROGRAMS\$ICONS_GROUP\ElegooSlicer.lnk"
  RMDir "$SMPROGRAMS\$ICONS_GROUP"

  RMDir /r "$INSTDIR"

  DeleteRegKey ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}"
  DeleteRegKey HKLM "${PRODUCT_DIR_REGKEY}"
  SetAutoClose true
  
SectionEnd

#-- 根据 NSIS 脚本编辑规则，所有 Function 区段必须放置在 Section 区段之后编写，以避免安装程序出现未可预知的问题。--#

Function un.onInit
  ; set shell context to all users for uninstallation
  SetShellVarContext all
  System::Call 'Kernel32::GetUserDefaultUILanguage() i.r0'
  ${If} $0 == ${LANG_CHINESE_SIMPLIFIED}
      StrCpy $LANGUAGE ${LANG_CHINESE_SIMPLIFIED}
  ${Else}
      StrCpy $LANGUAGE ${LANG_ENGLISH}
  ${EndIf}
  ;!insertmacro MUI_UNGETLANGUAGE
  ;在un.onInit 中只能调用un函数，在onInit中无法调用un，所以检测正在进行的检测 各自实现
  nsProcess::_FindProcess "elegoo-slicer.exe"
  Pop $R0
  ${If} $R0 = 0
  	${If} $LANGUAGE == ${LANG_ENGLISH}
	  MessageBox MB_OKCANCEL|MB_ICONSTOP "The installer has detected that ${PRODUCT_NAME} is running.$\nClick 'OK' to force close ${PRODUCT_NAME} and continue the installation.$\nClick 'Cancel' to exit the installer." /SD IDOK IDOK kill_and_continue IDCANCEL abort_install
	${Else}
	  MessageBox MB_OKCANCEL|MB_ICONSTOP "安装程序检测到 ${PRODUCT_NAME} 正在运行。是否要强制关闭它并继续安装?$\n点击 '确认' 强制关闭并继续安装，点击 '取消' 退出安装程序。" /SD IDOK IDOK kill_and_continue IDCANCEL abort_install
	${EndIf} 
  ${EndIf} 
  Goto done 

  abort_install:
    Abort 

  kill_and_continue:
    nsProcess::_KillProcess "elegoo-slicer.exe"
    Pop $R0
    Sleep 1000 

  done:
	${If} $LANGUAGE == ${LANG_ENGLISH}
	  MessageBox MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2 "Do you really want to completely remove $(^Name)?" /SD IDYES IDYES continue_uninstall IDNO abort_uninstall
	${Else}
	  MessageBox MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2 "您确定要完全卸载 $(^Name) 吗？" /SD IDYES IDYES continue_uninstall IDNO abort_uninstall
	${EndIf} 


  abort_uninstall:
    Abort
  
  continue_uninstall:
   
FunctionEnd


Function un.onUninstSuccess
  HideWindow
  MessageBox MB_ICONINFORMATION|MB_OK $(TXT_UNINSTALL_SUCCESS) /SD IDOK
FunctionEnd

