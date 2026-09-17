Unicode true

!define APPNAME "Space Calibrator"
!define ARPKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\SpaceCalibratorSmoothing"

Name "${APPNAME}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\OpenVR-SpaceCalibrator"
InstallDirRegKey HKCU "${ARPKEY}" "InstallLocation"
RequestExecutionLevel user
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"

!define MUI_ICON "..\src\overlay\SpaceCalibrator.ico"
!define MUI_UNICON "..\src\overlay\SpaceCalibrator.ico"

!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

VIProductVersion "${VIVERSION}"
VIAddVersionKey "ProductName" "${APPNAME}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "${APPNAME} Setup"
VIAddVersionKey "LegalCopyright" ""

!macro GuardRunning
	${Do}
		StrCpy $1 ""
		System::Call 'kernel32::OpenMutex(i 0x00100000, i 0, t "Global\MUTEX__SpaceCalibratorSmoothing") p .r0'
		${If} $0 P<> 0
			System::Call 'kernel32::CloseHandle(p r0)'
			StrCpy $1 "${APPNAME} is running. Close it, then try again."
			StrCpy $2 5
		${Else}
			nsExec::ExecToStack 'cmd /c tasklist /FI "IMAGENAME eq vrserver.exe" /NH | find /I "vrserver.exe"'
			Pop $0
			Pop $3
			${If} $0 = 0
				StrCpy $1 "SteamVR is running. Close it, then try again."
				StrCpy $2 6
			${EndIf}
		${EndIf}
		${If} $1 == ""
			${Break}
		${EndIf}
		${If} ${Silent}
			SetErrorLevel $2
			Quit
		${EndIf}
		MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "$1" IDRETRY +2
		Quit
	${Loop}
!macroend

Function .onInit
	!insertmacro GuardRunning
FunctionEnd

Function un.onInit
	!insertmacro GuardRunning
FunctionEnd

Section "Install"
	SetOutPath "$INSTDIR"
	File /r /x install.ps1 /x uninstall.ps1 "${PAYLOAD}\*"
	WriteUninstaller "$INSTDIR\Uninstall.exe"
	CreateShortcut "$SMPROGRAMS\${APPNAME}.lnk" "$INSTDIR\SpaceCalibrator.exe"
	WriteRegStr HKCU "${ARPKEY}" "DisplayName" "${APPNAME}"
	WriteRegStr HKCU "${ARPKEY}" "DisplayVersion" "${VERSION}"
	WriteRegStr HKCU "${ARPKEY}" "DisplayIcon" "$INSTDIR\SpaceCalibrator.exe"
	WriteRegStr HKCU "${ARPKEY}" "Publisher" "RealWhyKnot"
	WriteRegStr HKCU "${ARPKEY}" "InstallLocation" "$INSTDIR"
	WriteRegStr HKCU "${ARPKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
	WriteRegStr HKCU "${ARPKEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
	WriteRegStr HKCU "${ARPKEY}" "URLInfoAbout" "https://github.com/RealWhyKnot/OpenVR-SpaceCalibrator"
	WriteRegDWORD HKCU "${ARPKEY}" "NoModify" 1
	WriteRegDWORD HKCU "${ARPKEY}" "NoRepair" 1
	${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
	WriteRegDWORD HKCU "${ARPKEY}" "EstimatedSize" $0
	ExecWait '"$INSTDIR\SpaceCalibrator.exe" -register' $0
	${If} $0 = 2
		DetailPrint "SteamVR was not running; ${APPNAME} finishes its SteamVR setup the next time SteamVR starts."
		${IfNot} ${Silent}
			MessageBox MB_OK|MB_ICONINFORMATION "SteamVR was not running. ${APPNAME} finishes its SteamVR setup the next time SteamVR starts."
		${EndIf}
	${ElseIf} $0 = 3
		DetailPrint "SteamVR is not installed; ${APPNAME} finishes its SteamVR setup once SteamVR is installed."
		${IfNot} ${Silent}
			MessageBox MB_OK|MB_ICONINFORMATION "SteamVR is not installed yet. ${APPNAME} finishes its SteamVR setup once SteamVR is installed and started."
		${EndIf}
	${ElseIf} $0 <> 0
		DetailPrint "Driver registration did not complete (code $0)."
		${IfNot} ${Silent}
			MessageBox MB_OK|MB_ICONEXCLAMATION "${APPNAME} could not register its SteamVR driver. Open the app and use the Settings page to repair it."
		${EndIf}
	${EndIf}
SectionEnd

Section "Uninstall"
	ExecWait '"$INSTDIR\SpaceCalibrator.exe" -unregister'
	Delete "$SMPROGRAMS\${APPNAME}.lnk"
	DeleteRegKey HKCU "${ARPKEY}"
	DeleteRegKey HKCU "Software\Classes\Local Settings\Software\OpenVR-SpaceCalibrator"
	RMDir /r "$INSTDIR"
	RMDir /r "$LOCALAPPDATA\SpaceCalibrator"
	RMDir /r "$PROFILE\AppData\LocalLow\SpaceCalibrator"
SectionEnd
