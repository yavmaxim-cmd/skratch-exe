#define ProductVersion "0.9.0-beta"
#define ProductName "SKRATCH.EXE"
#ifndef Payload
  #error Define Payload as the absolute path to the release staging directory.
#endif
[Setup]
AppId=SKRATCH.EXE.NeedleAudio
AppName={#ProductName}
AppVersion={#ProductVersion}
AppPublisher=Needle Audio
VersionInfoVersion=0.9.0.0
VersionInfoTextVersion={#ProductVersion}
DefaultDirName={autopf}\Needle Audio\SKRATCH.EXE
DefaultGroupName=SKRATCH.EXE
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0
OutputBaseFilename=SKRATCH-EXE-0.9.0-beta-Windows-x64-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile={#Payload}\Licenses\LICENSE.txt
InfoBeforeFile={#Payload}\Installer-Readme.txt
UninstallDisplayName=SKRATCH.EXE {#ProductVersion}
CloseApplications=no
RestartApplications=no
SetupLogging=yes

[Types]
Name: "full"; Description: "All plug-ins and Standalone"
Name: "custom"; Description: "Choose components"; Flags: iscustom

[Components]
Name: "instrument"; Description: "SKRATCH.EXE Instrument (VST3)"; Types: full
Name: "fx"; Description: "SKRATCH.EXE FX (VST3)"; Types: full
Name: "standalone"; Description: "SKRATCH.EXE Standalone"; Types: full
Name: "demo"; Description: "Synthetic demonstration WAV"; Types: full

[Files]
Source: "{#Payload}\Windows\SKRATCH.EXE Instrument.vst3\*"; DestDir: "{commoncf64}\VST3\SKRATCH.EXE Instrument.vst3"; Components: instrument; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Payload}\Windows\SKRATCH.EXE FX.vst3\*"; DestDir: "{commoncf64}\VST3\SKRATCH.EXE FX.vst3"; Components: fx; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Payload}\Windows\SKRATCH.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#Payload}\Demo\Scratch-Demo.wav"; DestDir: "{app}\Demo"; Components: demo; Flags: ignoreversion
Source: "{#Payload}\Licenses\*"; DestDir: "{app}\Licenses"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Payload}\Docs\*"; DestDir: "{app}\Docs"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Payload}\Tools\Export-BetaDiagnostics.ps1"; DestDir: "{app}\Tools"; Flags: ignoreversion

[Icons]
Name: "{group}\SKRATCH.EXE"; Filename: "{app}\SKRATCH.exe"; Components: standalone
Name: "{group}\Quick Start"; Filename: "{app}\Docs\QUICK_START.md"

[Registry]
Root: HKLM64; Subkey: "Software\Needle Audio\SKRATCH.EXE\Installer"; ValueType: dword; ValueName: "Instrument"; ValueData: "1"; Components: instrument; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM64; Subkey: "Software\Needle Audio\SKRATCH.EXE\Installer"; ValueType: dword; ValueName: "FX"; ValueData: "1"; Components: fx; Flags: uninsdeletevalue uninsdeletekeyifempty

[Code]
function HasUnmanagedBundle(const Bundle, ValueName: String): Boolean;
var
  Owned: Cardinal;
  Path: String;
begin
  Path := ExpandConstant('{commoncf64}\VST3\') + Bundle;
  Owned := 0;
  RegQueryDWordValue(HKLM64, 'Software\Needle Audio\SKRATCH.EXE\Installer', ValueName, Owned);
  Result := (Owned <> 1) and (DirExists(Path) or FileExists(Path));
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if WizardIsComponentSelected('instrument') and
      HasUnmanagedBundle('SKRATCH.EXE Instrument.vst3', 'Instrument') then
    Result := 'An existing Instrument VST3 is outside installer management.'
  else if WizardIsComponentSelected('fx') and
      HasUnmanagedBundle('SKRATCH.EXE FX.vst3', 'FX') then
    Result := 'An existing FX VST3 is outside installer management.';
  if Result <> '' then
    Result := Result + #13#10 + 'Close the DAW and move the existing .vst3 bundle to a backup directory, then retry.';
end;
