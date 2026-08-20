param(
    [switch]$DevelopmentArchive,
    [string]$BuildDirectory = "build/windows-release",
    [string]$OutputDirectory = "artifacts/windows"
)

$ErrorActionPreference = "Stop"
if (-not $DevelopmentArchive) { throw "Milestone 1 supports only -DevelopmentArchive" }

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$build = Join-Path $root $BuildDirectory
$output = Join-Path $root $OutputDirectory
$stage = Join-Path $output "PadFlow-Windows-x64-Development-Unsigned"
$executable = Join-Path $build "bin/PadFlow.exe"
$vst3 = Join-Path $build "lib/VST3/PadFlow.vst3"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw "Missing $executable" }
if (-not (Test-Path -LiteralPath $vst3 -PathType Container)) { throw "Missing $vst3" }

if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage "Standalone") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage "VST3") | Out-Null
Copy-Item -LiteralPath $executable -Destination (Join-Path $stage "Standalone")
Copy-Item -LiteralPath $vst3 -Destination (Join-Path $stage "VST3") -Recurse
Copy-Item -LiteralPath (Join-Path $root "README.md") -Destination $stage
Copy-Item -LiteralPath (Join-Path $root "docs/CLIENT_INSTALLATION.md") -Destination $stage
Copy-Item -LiteralPath (Join-Path $root "docs/FL_STUDIO_VALIDATION.md") -Destination $stage
Copy-Item -LiteralPath (Join-Path $root "LICENSE.md") -Destination $stage
Copy-Item -LiteralPath (Join-Path $root "THIRD_PARTY_LICENSES.md") -Destination $stage
Copy-Item -LiteralPath (Join-Path $root "scripts/distribution/Install-PadFlow-Windows.cmd") -Destination $stage
Copy-Item -LiteralPath (Join-Path $root "scripts/distribution/Install-PadFlow-Windows.ps1") -Destination $stage
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText(
    (Join-Path $stage "UNSIGNED.txt"),
    "Unsigned Milestone 1 development build.`n",
    $utf8NoBom)
$manifest = @{
    product = "PadFlow"
    version = "0.1.0"
    platform = "windows-x64"
    architectures = @("x86_64")
    formats = @("Standalone", "VST3")
    runtime = "MSVC static"
    signed = $false
} |
    ConvertTo-Json
[System.IO.File]::WriteAllText(
    (Join-Path $stage "build-manifest.json"),
    "$manifest`n",
    $utf8NoBom)

$archive = Join-Path $output "PadFlow-Windows-x64-Development-Unsigned.zip"
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive }
Compress-Archive -LiteralPath $stage -DestinationPath $archive
Get-FileHash -LiteralPath $archive -Algorithm SHA256 |
    ForEach-Object { "$($_.Hash.ToLower())  $(Split-Path -Leaf $archive)" } |
    Set-Content -LiteralPath (Join-Path $output "SHA256SUMS-Windows.txt") -Encoding ascii
