; SPDX-License-Identifier: GPL-3.0-or-later
; Before installing or uninstalling, nothing of OpenRevers may be running:
; Windows will not replace a DLL that a process has loaded.
; - openrevers-cli.exe: the MCP servers that AI clients (Claude, etc.) start
;   and keep alive. They hold no work of the person's (in app mode the work
;   lives in the app), so they are ended; the client starts them again.
; - OpenRevers.exe: the app may have unsaved changes, so the person is asked
;   to close it (Retry) or to stop the installer (Cancel).
!ifndef ORV_CLOSE_RUNNING_NSH
!define ORV_CLOSE_RUNNING_NSH
!include LogicLib.nsh

!macro OrvCloseRunning
  DetailPrint "Cerrando servidores MCP de OpenRevers (openrevers-cli.exe)..."
  nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /F /T /IM openrevers-cli.exe'
  Pop $0
  ${Do}
    nsExec::Exec '"$SYSDIR\cmd.exe" /C "$SYSDIR\tasklist.exe" /NH /FI "IMAGENAME eq OpenRevers.exe" | "$SYSDIR\find.exe" /I "OpenRevers.exe"'
    Pop $0
    ${If} $0 != 0
      ${Break}
    ${EndIf}
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "OpenRevers está abierto. Guarda tu trabajo, ciérralo y pulsa Reintentar.$\n$\nOpenRevers is open. Save your work, close it and press Retry." /SD IDCANCEL IDRETRY +2
    Abort
  ${Loop}
  ; Windows may take a moment to release the files of the ended processes.
  Sleep 800
!macroend
!endif
