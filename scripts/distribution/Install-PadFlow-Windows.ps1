param([switch]$AllUsers, [switch]$Uninstall)

$ErrorActionPreference = "Stop"
$baseDirectory = if ($AllUsers) {
    Join-Path $env:CommonProgramFiles "VST3"
} else {
    Join-Path $env:LOCALAPPDATA "Programs\Common\VST3"
}
$destination = Join-Path $baseDirectory "PadFlow.vst3"
$source = Join-Path $PSScriptRoot "VST3\PadFlow.vst3"
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
$isAdministrator = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if ($AllUsers -and -not $isAdministrator) {
    throw "The -AllUsers option requires an elevated PowerShell window."
}
if ($Uninstall) {
    if (Test-Path -LiteralPath $destination) {
        Remove-Item -LiteralPath $destination -Recurse -Force
    }
    Write-Host "Removed $destination"
    exit 0
}
if (-not (Test-Path -LiteralPath $source -PathType Container)) {
    throw "Missing packaged VST3 bundle: $source"
}
New-Item -ItemType Directory -Force -Path $baseDirectory | Out-Null
if (Test-Path -LiteralPath $destination) {
    Remove-Item -LiteralPath $destination -Recurse -Force
}
Copy-Item -LiteralPath $source -Destination $destination -Recurse
Write-Host "Installed PadFlow VST3 to $destination"
Write-Host "In FL Studio Plugin Manager, add $baseDirectory if needed, then run Find installed plugins."
