Unicode true

; Tests use the silent install feature to bypass message box prompts.
SilentInstall silent

; This is the executable that will be run by an xpcshell test
OutFile "test_stub_installer.exe"


; The executable will write output to this file, and the xpcshell test
; will read the contents of the file to determine whether the tests passed
!define TEST_OUTPUT_FILENAME .\test_installer_output.txt


!include "stub_shared_defs.nsh"

; Some variables used only by tests and mocks
Var Stdout
Var FailureMessage
Var TestBreakpointNumber

; For building the test exectuable, this version of the IsTestBreakpointSet macro
; checks the value of the TestBreakpointNumber. For real installer executables,
; this macro is empty, and does nothing. (See stub.nsi for the that version.)
!macro IsTestBreakpointSet breakpointNumber
    ${If} ${breakpointNumber} == $TestBreakpointNumber
        Return
    ${EndIf}
!macroend

!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "TextFunc.nsh"
!include "WinVer.nsh"
!include "WordFunc.nsh"


Function ExitProcess
FunctionEnd

Function AttachConsole
    ; NSIS doesn't attach a console to the installer, so we'll do that now
    System::Call 'kernel32::AttachConsole(i -1)i.r0'
    ${If} $0 != 0
        ; Windows STD_OUTPUT_HANDLE is defined as -11
        System::Call 'kernel32::GetStdHandle(i -11)i.r0'
        Push $0
    ${Else}
        ; If there's no console, exit with a nonzero exit code
        System::Call 'kernel32::ExitProcess(i 65536)'
    ${EndIf}
FunctionEnd

; ${AtLeastWin10} is a macro provided by WinVer.nsh that can act as the predicate of a LogicLib
; control statement. This macro redefinition makes this test mockable
Var MockAtLeastWin10
!define /redef AtLeastWin10 `$MockAtLeastWin10 IsMockedAsTrue ""`

; See stub.nsi for the real version of CheckCpuSupportsSSE
Var MockCpuHasSSE
Function CheckCpuSupportsSSE
  StrCpy $CpuSupportsSSE "$MockCpuHasSSE"
FunctionEnd

; See stub.nsi for the real version of CanWrite
Var MockCanWrite
Function CanWrite
  StrCpy $CanWriteToInstallDir $MockCanWrite
FunctionEnd

!macro _IsMockedAsTrue _v _b _t _f
  ; If the mock value is the same as 'true', jump to the label specified in $_t
  ; Otherwise, jump to the label specified in $_f
  ; (This is compatible with LogicLib ${If} and similar tests)
  StrCmp `${_v}` 'true' `${_t}` `${_f}`
!macroend

; A helpful macro for testing that a variable is equal to a value.
; Provide the variable name (bare, without $ prefix) and the expected value.
; For example, to test that $MyVariable is equal to "hello there", you would write:
; !insertmacro AssertEqual MyVariable "hello there"
!macro AssertEqual _variableName _expectedValue
    ${If} "$${_variableName}" != "${_expectedValue}" ; quoted to prevent numeric coercion (01 != 1)
        StrCpy $FailureMessage "At Line ${__LINE__}: Expected ${_variableName} of ${_expectedValue} , got $${_variableName}"
        SetErrors
        Return
    ${EndIf}
!macroend

Var TestFailureCount

!macro UnitTest _testFunctionName
    Call ${_testFunctionName}
    IfErrors 0 +3
    IntOp $TestFailureCount $TestFailureCount + 1
    FileWrite $Stdout "FAILURE: $FailureMessage; "
    ClearErrors
!macroend

; Redefine ElevateUAC as a no-op in this test exectuable
!define /redef ElevateUAC ``

!include stub.nsh


; .onInit is responsible for running the tests
Function .onInit
    Call AttachConsole
    Pop $Stdout
    IntOp $TestFailureCount 0 + 0
    !insertmacro UnitTest TestDontInstallOnOldWindows

    !insertmacro UnitTest TestDontInstallOnNewWindowsWithoutSSE

    !insertmacro UnitTest TestDontInstallOnOldWindowsWithoutSSE

    !insertmacro UnitTest TestGetArchToInstall

    !insertmacro UnitTest TestMaintServiceCfg

    !insertmacro UnitTest TestCanWriteToDirSuccess

    !insertmacro UnitTest TestCanWriteToDirFail

    !insertmacro UnitTest TestUninstallRegKey

    ${If} $TestFailureCount = 0
        ; On success, write the success metric and jump to the end
        FileWrite $Stdout "All stub installer tests passed"
    ${Else}
        FileWrite $Stdout "$TestFailureCount stub installer tests failed"
    ${EndIf}
    FileClose $Stdout
    Return
OnError:
    Abort "Failed to run tests."
FunctionEnd

; Expect installation to abort if windows version < 10
Function TestDontInstallOnOldWindows
    StrCpy $MockAtLeastWin10 'false'
    StrCpy $MockCpuHasSSE '1'
    StrCpy $AbortInstallation ''
    StrCpy $ExitCode "Unknown"
    Call CommonOnInit
    !insertmacro AssertEqual ExitCode "${ERR_PREINSTALL_SYS_OS_REQ}"
    !insertmacro AssertEqual AbortInstallation "true"
    !insertmacro AssertEqual R7 "$(WARN_MIN_SUPPORTED_OSVER_MSG)"
FunctionEnd


; Expect installation to abort on processor without SSE, WIndows 10+ version
Function TestDontInstallOnNewWindowsWithoutSSE
    StrCpy $MockAtLeastWin10 'true'
    StrCpy $MockCpuHasSSE '0'
    StrCpy $AbortInstallation ''
    StrCpy $ExitCode "Unknown"
    Call CommonOnInit
    !insertmacro AssertEqual ExitCode "${ERR_PREINSTALL_SYS_HW_REQ}"
    !insertmacro AssertEqual AbortInstallation "true"
FunctionEnd

; Expect installation to abort on processor without SSE, Windows <10 version
Function TestDontInstallOnOldWindowsWithoutSSE
    StrCpy $MockAtLeastWin10 'false'
    StrCpy $MockCpuHasSSE '0'
    StrCpy $AbortInstallation ''
    StrCpy $ExitCode "Unknown"
    Call CommonOnInit
    !insertmacro AssertEqual ExitCode "${ERR_PREINSTALL_SYS_OS_REQ}"
    !insertmacro AssertEqual AbortInstallation "true"
    !insertmacro AssertEqual R7 "$(WARN_MIN_SUPPORTED_OSVER_CPU_MSG)"
FunctionEnd

; Expect to find a known supported architecture for Windows
Function TestGetArchToInstall
    StrCpy $TestBreakpointNumber "${TestBreakpointArchToInstall}"
    StrCpy $MockAtLeastWin10 'true'
    StrCpy $MockCpuHasSSE '1'
    StrCpy $INSTDIR "Unknown"
    StrCpy $ArchToInstall "Unknown"

    Call CommonOnInit
    ${Switch} $ArchToInstall
        ${Case} "${ARCH_X86}"
            ; OK, fall through
        ${Case} "${ARCH_AMD64}"
            ; OK, fall through
        ${Case} "${ARCH_AARCH64}"
            ; OK
            ${Break}
        ${Default}
            StrCpy $FailureMessage "Unexpected value for ArchToInstall: $ArchToInstall"
            SetErrors
            Return
    ${EndSwitch}

    ${Switch} $INSTDIR
        ${Case} "${DefaultInstDir64bit}"
            ; OK, fall through
        ${Case} "${DefaultInstDir32bit}"
            ; OK
            ${Break}
        ${Default}
            StrCpy $FailureMessage "Unexpected value for INSTDIR: $INSTDIR"
            SetErrors
            Return
    ${EndSwitch}
FunctionEnd

; Since we're not actually elevating permissions, expect not to enable installing maintenance service
Function TestMaintServiceCfg
    StrCpy $TestBreakpointNumber "${TestBreakpointMaintService}"
    StrCpy $CheckboxInstallMaintSvc 'Unknown'
    StrCpy $MockAtLeastWin10 'true'
    StrCpy $MockCpuHasSSE '1'

    Call CommonOnInit

    !insertmacro AssertEqual CheckboxInstallMaintSvc "0"
FunctionEnd

; Expect success if we can write to installation directory
Function TestCanWriteToDirSuccess
    StrCpy $TestBreakpointNumber "${TestBreakpointCanWriteToDir}"
    StrCpy $MockCanWrite 'true'
    StrCpy $MockAtLeastWin10 'true'
    StrCpy $MockCpuHasSSE '1'
    StrCpy $CanWriteToInstallDir "Unknown"
    StrCpy $AbortInstallation "false"

    Call CommonOnInit

    ; Since we're not running as admin, expect directory to be under user's own home directory, so it should be writable
    !insertmacro AssertEqual CanWriteToInstallDir "true"
    !insertmacro AssertEqual AbortInstallation "false"
FunctionEnd

; Expect failure if we can't write to installation directory
Function TestCanWriteToDirFail
    StrCpy $TestBreakpointNumber "${TestBreakpointCanWriteToDir}"
    StrCpy $MockCanWrite 'false'
    StrCpy $MockAtLeastWin10 'true'
    StrCpy $MockCpuHasSSE '1'
    StrCpy $CanWriteToInstallDir "Unknown"
    StrCpy $AbortInstallation "false"

    Call CommonOnInit

    ; Since we're not running as admin, expect directory to be under user's own home directory, so it should be writable
    !insertmacro AssertEqual CanWriteToInstallDir "false"
    !insertmacro AssertEqual ExitCode "${ERR_PREINSTALL_NOT_WRITABLE}"
    !insertmacro AssertEqual AbortInstallation "true"
FunctionEnd

!include postupdate_helper.nsh

Function TestUninstallRegKey
    Call CommonOnInit

    Push $INSTDIR ; back up the original contents for later

    ; Ensure that the getDefaultInstallDir function does not modify the
    ; contents of $1.
    Push $1
    Call getDefaultInstallDir
    Pop $INSTDIR
    Pop $INSTDIR
    !insertmacro AssertEqual INSTDIR "$1"

    ; Later, we also want to check that none of the common registers have been
    ; altered by calling functions. We save their contents on the stack for
    ; that check.
    Push "(0 $0, 1 $1, 2 $2, 3 $3, 4 $4, 5 $5, 6 $6, 7 $7, 8 $8, 9 $9)"

    ; ----
    ; Modify the INSTDIR in a way as if a user had chosen to add a version
    ; number to the path
    UserInfo::GetAccountType
    Pop $INSTDIR

    ${If} $INSTDIR == "User"
        ${GetLocalAppDataFolder} $INSTDIR
        StrCpy $INSTDIR "$INSTDIR\${BrandFullName} 123.0\" ; %LOCALAPPDATA%/${BrandFullName}
    ${Else}
        !ifdef HAVE_64BIT_BUILD
          StrCpy $INSTDIR "$PROGRAMFILES64\${BrandFullNameInternal} 123.0\"
        !else
          StrCpy $INSTDIR "$PROGRAMFILES32\${BrandFullNameInternal} 123.0\"
        !endif
    ${EndIf}
    Push ${buildNumWin10}
    Call getUninstallKey
    Pop $INSTDIR ; reuse INSTDIR for the return value
    !insertmacro AssertEqual INSTDIR \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\${BrandFullNameInternal} ${AppVersion} (${ARCH} ${AB_CD})"
    ClearErrors

    ; ----
    ; Modify the INSTDIR as if we wanted to install to
    ; (like unelevated installations do it)
    Push ${buildNumWin10}
    Call getUninstallKey
    Pop $INSTDIR ; reuse INSTDIR for the return value
    !insertmacro AssertEqual INSTDIR \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\${BrandFullNameInternal} ${AppVersion} (${ARCH} ${AB_CD})"
    ClearErrors

    ; ----
    ; An empty installation path should lead to the legacy uninstall key.
    StrCpy $INSTDIR ""
    Push ${buildNumWin10}
    Call getUninstallKey
    Pop $INSTDIR ; reuse INSTDIR for the return value
    !insertmacro AssertEqual INSTDIR \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\${BrandFullNameInternal} ${AppVersion} (${ARCH} ${AB_CD})"
    ClearErrors

    ; ----
    ; Modify the INSTDIR as if it was not touched by a user (use the default
    ; settings), which under Windows 10 is a special case and the Uninstall
    ; key should be rewritten, leaving the Version number out of it.
    Call getDefaultInstallDir ; pushes the default directory on the stack
        Pop $INSTDIR
    Push ${buildNumWin10}
    Call getUninstallKey
    Pop $INSTDIR ; reuse INSTDIR for the return value
    !insertmacro AssertEqual INSTDIR \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\${BrandFullNameInternal}"
    ClearErrors

    ; ----
    ; The same test again, but this time simulate Windows 11. The path is
    ; supposed to contain a version number. This restriction is expected to be
    ; removed in the future.
    Push ${buildNumWin11}
    Call getUninstallKey
    Pop $INSTDIR ; reuse INSTDIR for the return value
    !insertmacro AssertEqual INSTDIR \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\${BrandFullNameInternal} ${AppVersion} (${ARCH} ${AB_CD})"
    ClearErrors

    ; ----
    ; Special case: The maximal supported path length here is 1024 chars, in
    ; which case the return value for the path comparison is an error value.
    ; Since this error value will never match the default path name, we expect
    ; the same return value as we would get with an unsupported OS version.
    StrCpy $INSTDIR ""
    StrCpy $INSTDIR "$INSTDIR--------------------------------------------------"
    StrCpy $INSTDIR "$INSTDIR--------------------------------------------------"
    StrCpy $INSTDIR "$INSTDIR--------------------------------------------------"
    StrCpy $INSTDIR "$INSTDIR--------------------------------------------------"
    StrCpy $INSTDIR "$INSTDIR--------------------------------------------------"
    StrCpy $INSTDIR "$INSTDIR--------------------------------------------------"
    StrCpy $INSTDIR "$INSTDIR--------------------------------------------------"
    StrCpy $INSTDIR "$INSTDIR--------------------------------------------------"
    StrCpy $INSTDIR "$INSTDIR--------------------------------------------------"
    StrCpy $INSTDIR "$INSTDIR--------------------------------------------------"
    StrCpy $INSTDIR "$INSTDIR--------------------------------------------------"
    StrCpy $INSTDIR "$INSTDIR--------------------------------------------------"
    StrCpy $INSTDIR "$INSTDIR--------------------------------------------------"
    StrCpy $INSTDIR "$INSTDIR--------------------------------------------------"
    StrCpy $INSTDIR "$INSTDIR--------------------------------------------------"
    StrCpy $INSTDIR "$INSTDIR--------------------------------------------------"
    StrCpy $INSTDIR "$INSTDIR--------------------------------------------------"
    StrCpy $INSTDIR "$INSTDIR--------------------------------------------------"
    StrCpy $INSTDIR "$INSTDIR--------------------------------------------------"
    StrCpy $INSTDIR "$INSTDIR--------------------------------------------------"
    StrCpy $INSTDIR "$INSTDIR-----------------------" ; 1023 - chars

    ; 1st test: Just a sanity check to see if the block above really has 1023
    ; minus signs.
    push $0 ; save $0
    StrLen $0 $INSTDIR
    !insertmacro AssertEqual 0 1023
    Pop $0 ; restore $0

    ; 2nd test: getNormalizedPath is supposed to return the error message
    ; instead of a path.
    push $0 ; save $0
    ; test the one error case specifically that getNormalizedPath can return.
    Push $INSTDIR ; Simulate Windows 10
    Call getNormalizedPath
    Pop $0

    !insertmacro AssertEqual 0 \
        "[!] GetFullPathNameW: Insufficient buffer memory."
    ${IfNot} ${Errors}
      StrCpy $FailureMessage "At Line ${__LINE__}. An error was expected. Is a SetError missing?"
      SetErrors
      Return
    ${EndIf}
    ClearErrors
    Pop $0 ; restore $0

    ; 3rd test: getUninstallKey should return the legacy uninstall key with
    ; this buffer overflow.
    push $0 ; save $0
    Push 10240 ; Simulate Windows 10
    Call getUninstallKey
    Pop $0
    !insertmacro AssertEqual 0  \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\${BrandFullNameInternal} ${AppVersion} (${ARCH} ${AB_CD})"
    ClearErrors
    Pop $0 ; restore $0

    ; ----
    ; Make sure that after all the testing the common working registers still
    ; have their original values...
    Pop $INSTDIR
    ${If} "$INSTDIR" != "(0 $0, 1 $1, 2 $2, 3 $3, 4 $4, 5 $5, 6 $6, 7 $7, 8 $8, 9 $9)"
      StrCpy $FailureMessage "At Line ${__LINE__}  Registers were changed, we expected $\n"
      StrCpy $FailureMessage "$FailureMessage    '$INSTDIR' but received $\n"
      StrCpy $FailureMessage "$FailureMessage    '(0 $0, 1 $1, 2 $2, 3 $3, 4 $4, 5 $5, 6 $6, 7 $7, 8 $8, 9 $9)'"
      SetErrors
      Return
    ${EndIf}
    Pop $INSTDIR
FunctionEnd

Section
SectionEnd
