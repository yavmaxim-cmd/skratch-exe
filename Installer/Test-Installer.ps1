[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $Installer,
    [Parameter(Mandatory)]
    [switch] $DisposableVm
)

$ErrorActionPreference = 'Stop'
if (-not $DisposableVm) { throw 'Run only on a disposable Windows x64 VM.' }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Use an elevated terminal inside the VM.'
}
$installerPath = (Resolve-Path -LiteralPath $Installer).Path
$vst = Join-Path $env:CommonProgramFiles 'VST3'
$instrument = Join-Path $vst 'SKRATCH.EXE Instrument.vst3'
$effect = Join-Path $vst 'SKRATCH.EXE FX.vst3'
$app = Join-Path $env:ProgramFiles 'Needle Audio\SKRATCH.EXE'
foreach ($path in @($instrument, $effect, $app)) {
    if (Test-Path -LiteralPath $path) { throw "Expected a clean VM. Existing path: $path" }
}

$probe = Join-Path $env:APPDATA ('SKRATCH-beta-preset-test-' + [Guid]::NewGuid())
New-Item -ItemType Directory -Path $probe | Out-Null
$preset = Join-Path $probe 'User.skratch'
[IO.File]::WriteAllText($preset, 'preset retention sentinel')
$expected = (Get-FileHash -LiteralPath $preset).Hash
function Assert-Preset {
    if ((Get-FileHash -LiteralPath $preset).Hash -ne $expected) { throw 'User preset changed.' }
}
function Invoke-Setup([string] $Components) {
    $log = Join-Path $probe ('setup-' + [Guid]::NewGuid() + '.log')
    $arguments = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /COMPONENTS="' +
        $Components + '" /LOG="' + $log + '"'
    (Start-Process -FilePath $installerPath -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden).ExitCode
}
function Assert-Install([string] $Bundle, [string] $Name) {
    $binary = Join-Path $Bundle "Contents\x86_64-win\$Name"
    if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) { throw "Missing binary: $binary" }
}
function Invoke-Uninstall {
    $uninstaller = Join-Path $app 'unins000.exe'
    $process = Start-Process -FilePath $uninstaller -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART' -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0) { throw "Uninstall returned $($process.ExitCode)." }
    if ((Test-Path -LiteralPath $instrument) -or (Test-Path -LiteralPath $effect) -or
        (Test-Path -LiteralPath (Join-Path $app 'SKRATCH.exe'))) { throw 'Installed program files remain.' }
    Assert-Preset
}

if ((Invoke-Setup 'instrument') -ne 0) { throw 'Instrument-only install failed.' }
Assert-Install $instrument 'SKRATCH.EXE Instrument.vst3'
if ((Test-Path -LiteralPath $effect) -or (Test-Path -LiteralPath (Join-Path $app 'SKRATCH.exe'))) {
    throw 'Unselected components were installed.'
}
Assert-Preset
Invoke-Uninstall

if ((Invoke-Setup 'instrument,fx,standalone,demo') -ne 0) { throw 'Full reinstall failed.' }
Assert-Install $instrument 'SKRATCH.EXE Instrument.vst3'
Assert-Install $effect 'SKRATCH.EXE FX.vst3'
if (-not (Test-Path -LiteralPath (Join-Path $app 'Demo\Scratch-Demo.wav'))) { throw 'Demo missing.' }
$standalone = Start-Process -FilePath (Join-Path $app 'SKRATCH.exe') -ArgumentList '--smoke-test' -Wait -PassThru -WindowStyle Hidden
if ($standalone.ExitCode -ne 0) { throw "Standalone smoke test returned $($standalone.ExitCode)." }
$localPresets = Join-Path $app 'Presets'
[IO.Directory]::CreateDirectory($localPresets) | Out-Null
$localPreset = Join-Path $localPresets 'Saved-by-user.skratch'
[IO.File]::WriteAllText($localPreset, 'untracked user preset')
if ((Invoke-Setup 'instrument,fx,standalone,demo') -ne 0) { throw 'Installer-managed update failed.' }
Assert-Preset
if ([IO.File]::ReadAllText($localPreset) -ne 'untracked user preset') { throw 'Local preset changed on upgrade.' }
Invoke-Uninstall
if ([IO.File]::ReadAllText($localPreset) -ne 'untracked user preset') { throw 'Local preset changed on uninstall.' }

[IO.Directory]::CreateDirectory($effect) | Out-Null
$unmanaged = Join-Path $effect 'unmanaged-sentinel.txt'
[IO.File]::WriteAllText($unmanaged, 'existing third-party copy sentinel')
if ((Invoke-Setup 'fx') -eq 0 -or [IO.File]::ReadAllText($unmanaged) -ne 'existing third-party copy sentinel') {
    throw 'Unmanaged bundle protection failed.'
}
Assert-Preset
"PASS: component selection, reinstall, upgrade, uninstall, retained preset and unmanaged-bundle protection."
"VM evidence: $probe"
