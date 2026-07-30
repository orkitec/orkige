;********************************************************************
;	created:	Thursday 2026/07/30 at 12:00
;	filename: 	orkige_installer.nsi
;	author:		steffen.roemer
;	notice:		This source file is part of orkige (orkitec Game engine)
;				For the latest info, see http://www.orkitec.com/
;	copyright:	(c) 2009-2026 orkitec
;********************************************************************
;
; The Windows installer for the Orkige editor, compiled by makensis and
; driven entirely from the command line by Util/orkige_nightly_package.py -
; the script itself holds no build-specific value, so reviewing it is reading
; this one file and nothing else.
;
; PER-USER, by design: everything lands under %LOCALAPPDATA%\Programs and
; HKEY_CURRENT_USER, so installing needs no administrator elevation and no
; consent prompt. A per-machine install would buy nothing here (one person
; uses an editor) and would cost every installer run an elevation dialog on
; top of the unsigned-publisher warning.
;
; What it installs is the SAME staged directory the .zip is made from, copied
; verbatim - including the app-local Visual C++ runtime DLLs the packager
; places beside the executable - so the installed editor and the unpacked zip
; are the same tree in two containers.
;
; Defines the packager passes with /D:
;   STAGE_DIR         the staged directory whose contents are installed
;   OUT_FILE          the installer executable to write
;   ORKIGE_VERSION    the ordered version, shown in Installed apps
;   FILE_VERSION      the same version as a numeric a.b.c.d for the VERSIONINFO
;                     resource (Windows accepts nothing else there)
;   INSTALL_SIZE_KB   the installed size, for the Installed apps size column

Unicode true

!ifndef STAGE_DIR
  !error "STAGE_DIR is not defined - this script is driven by orkige_nightly_package.py"
!endif
!ifndef OUT_FILE
  !error "OUT_FILE is not defined"
!endif
!ifndef ORKIGE_VERSION
  !error "ORKIGE_VERSION is not defined"
!endif
!ifndef FILE_VERSION
  !error "FILE_VERSION is not defined"
!endif
!ifndef INSTALL_SIZE_KB
  !error "INSTALL_SIZE_KB is not defined"
!endif

!define PRODUCT "Orkige"
!define PUBLISHER "orkitec"
!define EDITOR_EXE "orkige_editor.exe"
; where Windows looks for the list it shows as Installed apps / Programs and
; Features. HKCU, because this is a per-user install.
!define UNINST_KEY \
  "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT}"

Name "${PRODUCT}"
Caption "${PRODUCT} ${ORKIGE_VERSION} Setup"
OutFile "${OUT_FILE}"
; %LOCALAPPDATA%\Programs is the per-user counterpart of Program Files
InstallDir "$LOCALAPPDATA\Programs\${PRODUCT}"
; a re-install offers wherever the previous one went
InstallDirRegKey HKCU "Software\${PRODUCT}" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUnInstDetails show

VIProductVersion "${FILE_VERSION}"
VIAddVersionKey "ProductName" "${PRODUCT}"
VIAddVersionKey "ProductVersion" "${ORKIGE_VERSION}"
VIAddVersionKey "FileVersion" "${FILE_VERSION}"
VIAddVersionKey "CompanyName" "${PUBLISHER}"
VIAddVersionKey "LegalCopyright" "(c) 2009-2026 ${PUBLISHER}"
VIAddVersionKey "FileDescription" "${PRODUCT} editor installer"

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "${PRODUCT}" SecMain
  SectionIn RO
  ; current user, never the all-users locations - the install is per-user and
  ; the shortcut has to land in THIS user's Start menu
  SetShellVarContext current
  SetOutPath "$INSTDIR"
  ; the staged tree verbatim: editor, player, texture cook tool, the app-local
  ; Visual C++ runtime DLLs, share\orkige\ and the VERSION / CHANGELOG.md /
  ; KNOWN-LIMITATIONS.md files. The wildcard is "*" rather than "*.*" because
  ; the tree carries extension-less files (VERSION), which the older spelling
  ; is not unambiguously defined to match.
  File /r "${STAGE_DIR}\*"

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  CreateDirectory "$SMPROGRAMS\${PRODUCT}"
  CreateShortCut "$SMPROGRAMS\${PRODUCT}\${PRODUCT}.lnk" "$INSTDIR\${EDITOR_EXE}"
  CreateShortCut "$SMPROGRAMS\${PRODUCT}\Uninstall ${PRODUCT}.lnk" "$INSTDIR\Uninstall.exe"

  WriteRegStr HKCU "Software\${PRODUCT}" "InstallDir" "$INSTDIR"

  ; the record Windows reads for Settings > Installed apps: without it the
  ; editor is installed but not listed, and cannot be removed from there
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "${PRODUCT}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${ORKIGE_VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\${EDITOR_EXE}"
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegStr HKCU "${UNINST_KEY}" "QuietUninstallString" "$\"$INSTDIR\Uninstall.exe$\" /S"
  WriteRegDWORD HKCU "${UNINST_KEY}" "EstimatedSize" ${INSTALL_SIZE_KB}
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  SetShellVarContext current

  Delete "$SMPROGRAMS\${PRODUCT}\${PRODUCT}.lnk"
  Delete "$SMPROGRAMS\${PRODUCT}\Uninstall ${PRODUCT}.lnk"
  RMDir "$SMPROGRAMS\${PRODUCT}"

  ; recursive removal is guarded on finding the editor where the install put
  ; it: a directory the user redirected somewhere unfortunate is left alone
  ; rather than emptied
  IfFileExists "$INSTDIR\${EDITOR_EXE}" 0 keep_dir
    Delete "$INSTDIR\Uninstall.exe"
    RMDir /r "$INSTDIR"
  keep_dir:

  DeleteRegKey HKCU "${UNINST_KEY}"
  DeleteRegKey HKCU "Software\${PRODUCT}"
SectionEnd
