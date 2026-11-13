<#
.SYNOPSIS
    Cross-compiles the game for Linux from Windows and zips it as a dedicated-server folder.

.DESCRIPTION
    Needs the UE 5.9 Linux cross-compile toolchain (v26_clang-20.1.8-rockylinux8, see README-linux.txt). Set
    LINUX_MULTIARCH_ROOT to its folder first, or pass -ToolchainRoot. Builds the McClient GAME target for Linux
    (Shipping), because an engine installed from the launcher can't build Server targets; the same binary runs as a
    dedicated server with -server (run-server.sh). Run it on a PC with the toolchain, never needs a Linux machine.

    Output: <OutDir>\McUnreal-Server-Linux-<Date>.zip. It holds none of Minecraft's files; the server needs the
    cache folder made on a PC that has Minecraft (README-linux.txt, "The cache").
#>
[CmdletBinding()]
param(
    [string]$OutDir = 'I:\mc-package-out',
    [string]$Date = (Get-Date -Format 'yyyy-MM-dd'),
    [string]$ToolchainRoot = $env:LINUX_MULTIARCH_ROOT
)
$ErrorActionPreference = 'Stop'
$EngineRoot = @('J:\UE_5.9\Engine', 'C:\Program Files\Epic Games\UE_5.9\Engine') | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $ToolchainRoot -or -not (Test-Path (Join-Path $ToolchainRoot 'x86_64-unknown-linux-gnu\bin\clang++.exe'))) {
    throw "No Linux toolchain: set LINUX_MULTIARCH_ROOT to the folder holding x86_64-unknown-linux-gnu (README-linux.txt)."
}
$env:LINUX_MULTIARCH_ROOT = $ToolchainRoot
$Project = Join-Path (Split-Path (Split-Path $PSScriptRoot)) 'McClient.uproject'
$Stage = Join-Path $OutDir "server-linux-$Date"
$Zip = Join-Path $OutDir "McUnreal-Server-Linux-$Date.zip"
New-Item -ItemType Directory -Force $OutDir | Out-Null
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }

& "$EngineRoot\Build\BatchFiles\RunUAT.bat" BuildCookRun "-project=$Project" -noP4 -platform=Linux -clientconfig=Shipping -target=McClient `
    -build -cook -stage -pak -archive "-archivedirectory=$Stage" -utf8output -nocompileeditor
if ($LASTEXITCODE -ne 0) { throw "BuildCookRun failed ($LASTEXITCODE)" }

# Nothing of Mojang's, and no editor AI plugins.
$files = Get-ChildItem -Recurse -File $Stage
$bad = $files | Where-Object {
    $rel = $_.FullName.Substring($Stage.Length)
    $rel -match '(^|[\/])cache([\/]|$)' -or $rel -match '(^|[\/])(assets|data|reports|blockstates)[\/]minecraft' -or
    $_.Extension -in '.ogg', '.jar', '.mcmeta', '.nbt' -or
    $_.FullName -match '(?i)(ModelContextProtocol|Toolset|AgentLook|AIAssistant)'
}
if ($bad) { $bad | ForEach-Object { Write-Error "Must not ship: $($_.FullName)" }; throw 'The stage holds files that must not be shared.' }

Copy-Item (Join-Path $PSScriptRoot 'run-server.sh') $Stage -Force
Copy-Item (Join-Path $PSScriptRoot 'mcserver.service') $Stage -Force
Copy-Item (Join-Path $PSScriptRoot 'README-linux.txt') $Stage -Force
if (Test-Path $Zip) { Remove-Item $Zip }
Compress-Archive -Path (Join-Path $Stage '*') -DestinationPath $Zip -CompressionLevel Optimal
$h = (Get-FileHash $Zip -Algorithm SHA256).Hash
"{0}  {1:N0} bytes  sha256 {2}" -f $Zip, (Get-Item $Zip).Length, $h
