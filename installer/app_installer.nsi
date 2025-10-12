; StayPutVR Installer Script
; For NSIS (Nullsoft Scriptable Install System)

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "WinVer.nsh"
!include "nsDialogs.nsh"

; Define installer name and output file
Name "StayPutVR"
OutFile "StayPutVR v1.1.1 Setup.exe"

; Default installation directory
InstallDir "$PROGRAMFILES\StayPutVR"

; Registry key for uninstaller
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\StayPutVR"

; Visual C++ Redistributable URL and registry detection
!define VCREDIST_URL "https://aka.ms/vs/17/release/vc_redist.x64.exe"
!define VCREDIST_KEY "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64"

; Request application privileges
RequestExecutionLevel admin

; Interface settings
!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

; Variables
Var Dialog
Var SteamVRPathLabel
Var SteamVRPathText
Var SteamVRBrowseButton
Var SteamVRPath
Var VCRedistInstalled
Var InstallVCRedist

; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
Page custom SteamVRPathPage SteamVRPathPageLeave
Page custom VCRedistPage VCRedistPageLeave
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Languages
!insertmacro MUI_LANGUAGE "English"

; Custom string replace function
Function StrReplaceS
  Exch $R2 ; replacement
  Exch 1
  Exch $R1 ; search string
  Exch 2
  Exch $R0 ; source string
  Push $R3
  Push $R4
  Push $R5
  Push $R6
  Push $R7
  Push $R8
  Push $R9

  StrCpy $R3 0
  StrLen $R4 $R1
  StrLen $R6 $R0
  StrLen $R9 $R2
  loop:
    StrCpy $R5 $R0 $R4 $R3
    StrCmp $R5 $R1 found
    StrCmp $R3 $R6 done
    IntOp $R3 $R3 + 1
    Goto loop
  found:
    StrCpy $R5 $R0 $R3
    IntOp $R8 $R3 + $R4
    StrCpy $R7 $R0 "" $R8
    StrCpy $R0 $R5$R2$R7
    IntOp $R3 $R3 + $R9
    IntOp $R6 $R6 - $R4
    IntOp $R6 $R6 + $R9
    Goto loop
  done:

  Pop $R9
  Pop $R8
  Pop $R7
  Pop $R6
  Pop $R5
  Pop $R4
  Pop $R3
  Exch $R0
  Exch 2
  Exch $R1
  Exch 1
  Exch $R2
FunctionEnd

; Function to detect Steam installation path
Function DetectSteamPath
    ReadRegStr $0 HKCU "Software\Valve\Steam" "SteamPath"
    ${If} $0 == ""
        StrCpy $0 "C:\Program Files (x86)\Steam"
    ${EndIf}
    ; Convert forward slashes to backslashes
    Push $0
    Push "/"
    Push "\"
    Call StrReplaceS
    Pop $0
    Return
FunctionEnd

; Function to detect SteamVR installation path
Function DetectSteamVRPath
    ; Always use the explicit default path as requested
    StrCpy $1 "C:\Program Files (x86)\Steam\steamapps\common\SteamVR"
    Return
FunctionEnd

; SteamVR Path selection page
Function SteamVRPathPage
    ; Try to detect SteamVR path first
    Call DetectSteamVRPath
    StrCpy $SteamVRPath $1
    
    !insertmacro MUI_HEADER_TEXT "SteamVR Location" "Please specify the location of your SteamVR installation."
    
    nsDialogs::Create 1018
    Pop $Dialog
    
    ${If} $Dialog == error
        Abort
    ${EndIf}
    
    ${NSD_CreateLabel} 0 0 100% 20u "Please specify the location of your SteamVR installation:"
    Pop $SteamVRPathLabel
    
    ${NSD_CreateLabel} 0 22u 100% 26u "Select the 'SteamVR' folder, typically located at:$\r$\n[Steam installation]\steamapps\common\SteamVR"
    Pop $0
    
    ${NSD_CreateText} 0 50u 70% 12u $SteamVRPath
    Pop $SteamVRPathText
    
    ${NSD_CreateBrowseButton} 75% 50u 25% 12u "Browse..."
    Pop $SteamVRBrowseButton
    ${NSD_OnClick} $SteamVRBrowseButton SteamVRBrowseButtonClick
    
    nsDialogs::Show
FunctionEnd

Function SteamVRBrowseButtonClick
    ${NSD_GetText} $SteamVRPathText $SteamVRPath
    nsDialogs::SelectFolderDialog "Select SteamVR Installation Folder" $SteamVRPath
    Pop $0
    ${If} $0 != error
        StrCpy $SteamVRPath $0
        ${NSD_SetText} $SteamVRPathText $SteamVRPath
    ${EndIf}
FunctionEnd

Function SteamVRPathPageLeave
    ${NSD_GetText} $SteamVRPathText $SteamVRPath
    
    ; Check if path exists
    IfFileExists "$SteamVRPath\*.*" PathExists
        MessageBox MB_ICONEXCLAMATION|MB_OK "The specified SteamVR path does not exist. Please select a valid path."
        Abort
    PathExists:
    
    ; Check if this looks like a SteamVR directory
    IfFileExists "$SteamVRPath\bin\*.*" PathValid
        MessageBox MB_ICONQUESTION|MB_YESNO "This doesn't appear to be a SteamVR directory. It should contain subfolders like 'bin', 'drivers', etc.$\n$\nAre you sure this is the correct SteamVR installation folder?" IDYES PathValid
        Abort
    PathValid:
    
FunctionEnd

; Function to check if VC++ Redistributable is installed
Function CheckVCRedist
    ; Default to not installed
    StrCpy $VCRedistInstalled "0"
    
    ; Check if VC++ 2015-2022 Redistributable is installed (x64)
    ReadRegDWORD $0 HKLM "${VCREDIST_KEY}" "Installed"
    ${If} $0 == "1"
        StrCpy $VCRedistInstalled "1"
    ${EndIf}
FunctionEnd

; Visual C++ Redistributable Page
Function VCRedistPage
    ; Check if VC++ Redistributable is already installed
    Call CheckVCRedist
    ${If} $VCRedistInstalled == "1"
        ; Skip the page if already installed
        Abort
    ${EndIf}
    
    ; Default to install
    StrCpy $InstallVCRedist "1"
    
    !insertmacro MUI_HEADER_TEXT "Visual C++ Redistributable" "Install Microsoft Visual C++ Redistributable (recommended)"
    
    nsDialogs::Create 1018
    Pop $Dialog
    
    ${If} $Dialog == error
        Abort
    ${EndIf}
    
    ${NSD_CreateLabel} 0 0 100% 40u "StayPutVR requires the Microsoft Visual C++ Redistributable to run properly. This component is not installed on your system.$\r$\n$\r$\nIt's recommended to install this component."
    Pop $0
    
    ${NSD_CreateCheckbox} 0 50u 100% 10u "Install Visual C++ Redistributable"
    Pop $InstallVCRedist
    ${NSD_Check} $InstallVCRedist
    
    nsDialogs::Show
FunctionEnd

Function VCRedistPageLeave
    ${NSD_GetState} $InstallVCRedist $InstallVCRedist
FunctionEnd

; Installation section
Section "Install"
    SetOutPath "$INSTDIR"
    
    ; Install Visual C++ Redistributable if needed and if user opted to install it
    Call CheckVCRedist
    ${If} $VCRedistInstalled == "0"
        ${If} $InstallVCRedist == "1"
            DetailPrint "Downloading Visual C++ Redistributable..."
            NSISdl::download "${VCREDIST_URL}" "$TEMP\vc_redist.x64.exe"
            Pop $0
            ${If} $0 == "success"
                DetailPrint "Installing Visual C++ Redistributable..."
                ExecWait '"$TEMP\vc_redist.x64.exe" /quiet /norestart' $0
                ${If} $0 != "0"
                    DetailPrint "Visual C++ Redistributable installation failed with code $0"
                    MessageBox MB_ICONEXCLAMATION|MB_OK "The Visual C++ Redistributable installation failed. StayPutVR may not work correctly without it.$\n$\nYou can download and install it manually from: ${VCREDIST_URL}"
                ${Else}
                    DetailPrint "Visual C++ Redistributable installed successfully"
                ${EndIf}
            ${Else}
                DetailPrint "Failed to download Visual C++ Redistributable: $0"
                MessageBox MB_ICONEXCLAMATION|MB_OK "Failed to download the Visual C++ Redistributable. StayPutVR may not work correctly without it.$\n$\nYou can download and install it manually from: ${VCREDIST_URL}"
            ${EndIf}
            ; Clean up
            Delete "$TEMP\vc_redist.x64.exe"
        ${EndIf}
    ${EndIf}
    
    ; Install application files
    CreateDirectory "$INSTDIR\bin"
    CreateDirectory "$INSTDIR\resources"
    
    SetOutPath "$INSTDIR\bin"
    File "build\bin\stayputvr_app.exe"
    
    ; Optional: Include runtime DLLs directly (useful for debug builds)
    ; Uncomment and modify these lines if you need to include specific DLLs
    ; File "C:\Windows\System32\msvcp140.dll"
    ; File "C:\Windows\System32\vcruntime140.dll"
    ; File "C:\Windows\System32\vcruntime140_1.dll"
    
    SetOutPath "$INSTDIR\resources"
    File /r "application\resources\*.*"
    
    ; Copy resources to AppData\Roaming location where the application will look for them
    CreateDirectory "$APPDATA\StayPutVR\resources"
    SetOutPath "$APPDATA\StayPutVR\resources"
    File /r "application\resources\*.*"
    
    ; Install driver files - using user-specified SteamVR path
    CreateDirectory "$SteamVRPath\drivers\stayputvr\bin\win64"
    
    SetOutPath "$SteamVRPath\drivers\stayputvr\bin\win64"
    File "build\driver_stayputvr.dll"
    
    ; Optional: Include runtime DLLs for the driver
    ; Uncomment and modify these lines if you need to include specific DLLs
    ; File "C:\Windows\System32\msvcp140.dll"
    ; File "C:\Windows\System32\vcruntime140.dll"
    ; File "C:\Windows\System32\vcruntime140_1.dll"
    
    SetOutPath "$SteamVRPath\drivers\stayputvr"
    File "driver.vrdrivermanifest"
    
    ; Register driver with SteamVR using vrpathreg from SteamVR's bin\win64 directory
    ExecWait '"$SteamVRPath\bin\win64\vrpathreg.exe" adddriver "$SteamVRPath\drivers\stayputvr"'
    
    ; Create shortcuts
    CreateDirectory "$SMPROGRAMS\StayPutVR"
    CreateShortcut "$SMPROGRAMS\StayPutVR\StayPutVR.lnk" "$INSTDIR\bin\stayputvr_app.exe"
    CreateShortcut "$SMPROGRAMS\StayPutVR\Uninstall.lnk" "$INSTDIR\uninstall.exe"
    
    ; Store SteamVR path for uninstaller
    WriteRegStr HKLM "${UNINSTKEY}" "SteamVRPath" "$SteamVRPath"
    
    ; Write uninstaller
    WriteUninstaller "$INSTDIR\uninstall.exe"
    
    ; Write uninstall information to registry
    WriteRegStr HKLM "${UNINSTKEY}" "DisplayName" "StayPutVR"
    WriteRegStr HKLM "${UNINSTKEY}" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
    WriteRegStr HKLM "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\bin\stayputvr_app.exe,0"
    WriteRegStr HKLM "${UNINSTKEY}" "Publisher" "StayPutVR Team"
    WriteRegStr HKLM "${UNINSTKEY}" "DisplayVersion" "1.1.1"
    
    ; Get size of installation directory
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD HKLM "${UNINSTKEY}" "EstimatedSize" "$0"
SectionEnd

; Uninstallation section
Section "Uninstall"
    ; Get stored SteamVR path
    ReadRegStr $SteamVRPath HKLM "${UNINSTKEY}" "SteamVRPath"
    ${If} $SteamVRPath == ""
        ; Fallback to detected path if registry entry is missing
        Call un.DetectSteamVRPath
        StrCpy $SteamVRPath $1
    ${EndIf}
    
    ; Unregister driver from SteamVR using vrpathreg from SteamVR's bin\win64 directory
    ExecWait '"$SteamVRPath\bin\win64\vrpathreg.exe" removedriver "$SteamVRPath\drivers\stayputvr"'
    
    ; Remove driver files
    RMDir /r "$SteamVRPath\drivers\stayputvr"
    
    ; Remove application files
    Delete "$INSTDIR\bin\stayputvr_app.exe"
    RMDir /r "$INSTDIR\resources"
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR\bin"
    RMDir "$INSTDIR"
    
    ; Remove application data
    RMDir /r "$APPDATA\StayPutVR"
    
    ; Remove shortcuts
    Delete "$SMPROGRAMS\StayPutVR\StayPutVR.lnk"
    Delete "$SMPROGRAMS\StayPutVR\Uninstall.lnk"
    RMDir "$SMPROGRAMS\StayPutVR"
    
    ; Remove registry entries
    DeleteRegKey HKLM "${UNINSTKEY}"
SectionEnd

; Uninstall functions
Function un.DetectSteamPath
    ReadRegStr $0 HKCU "Software\Valve\Steam" "SteamPath"
    ${If} $0 == ""
        StrCpy $0 "C:\Program Files (x86)\Steam"
    ${EndIf}
    ; Convert forward slashes to backslashes
    Push $0
    Push "/"
    Push "\"
    Call un.StrReplaceS
    Pop $0
    Return
FunctionEnd

Function un.DetectSteamVRPath
    ; Always use the explicit default path as requested
    StrCpy $1 "C:\Program Files (x86)\Steam\steamapps\common\SteamVR"
    Return
FunctionEnd

; Uninstaller version of the string replace function
Function un.StrReplaceS
  Exch $R2 ; replacement
  Exch 1
  Exch $R1 ; search string
  Exch 2
  Exch $R0 ; source string
  Push $R3
  Push $R4
  Push $R5
  Push $R6
  Push $R7
  Push $R8
  Push $R9

  StrCpy $R3 0
  StrLen $R4 $R1
  StrLen $R6 $R0
  StrLen $R9 $R2
  loop:
    StrCpy $R5 $R0 $R4 $R3
    StrCmp $R5 $R1 found
    StrCmp $R3 $R6 done
    IntOp $R3 $R3 + 1
    Goto loop
  found:
    StrCpy $R5 $R0 $R3
    IntOp $R8 $R3 + $R4
    StrCpy $R7 $R0 "" $R8
    StrCpy $R0 $R5$R2$R7
    IntOp $R3 $R3 + $R9
    IntOp $R6 $R6 - $R4
    IntOp $R6 $R6 + $R9
    Goto loop
  done:

  Pop $R9
  Pop $R8
  Pop $R7
  Pop $R6
  Pop $R5
  Pop $R4
  Pop $R3
  Exch $R0
  Exch 2
  Exch $R1
  Exch 1
  Exch $R2
FunctionEnd
