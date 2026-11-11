[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $OutputFile
)

$ErrorActionPreference = 'Stop'
$data = [ordered]@{
    schema = 1
    release = '0.9.0-beta'
    windowsVersion = [Environment]::OSVersion.Version.ToString()
    osArchitecture = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
}
$fullPath = [IO.Path]::GetFullPath($OutputFile)
$stream = [IO.File]::Open($fullPath, [IO.FileMode]::CreateNew)
try {
    $writer = [IO.StreamWriter]::new($stream, [Text.UTF8Encoding]::new($false))
    try {
        $writer.WriteLine(($data | ConvertTo-Json))
    } finally {
        $writer.Dispose()
    }
} finally {
    $stream.Dispose()
}
