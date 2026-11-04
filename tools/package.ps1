<#
.SYNOPSIS
    Packages the game (McClient, Win64 Shipping) and zips it for a friend.

.DESCRIPTION
    Cooks and stages with RunUAT BuildCookRun, checks that nothing from cache/ (Mojang's textures, sounds, models
    and data) or any other Mojang file went in, adds tools/package/README.txt and Play.bat, and zips it all to
    <OutDir>\McUnreal-<date>.zip. The friend's game builds its own cache from their own Minecraft install the first
    time it runs (McCacheSetup.h). Writes the list of staged files next to the zip. Never uploads anything.

    One build at a time on this PC: stop if an UnrealBuildTool, AutomationTool or editor process is running.

.EXAMPLE
    tools/package.ps1
    tools/package.ps1 -SkipBuild    # check and zip an existing stage
#>
[CmdletBinding()]
param(
    [string]$OutDir = 'E:\mc-unreal-builds',
    [string]$Date = (Get-Date -Format 'yyyy-MM-dd'),
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$EngineRoot = @('E:\Epic Games\UE_5.8\Engine', 'C:\UE_5.8\Engine') | Where-Object { Test-Path $_ } | Select-Object -First 1
$Project = Join-Path (Split-Path $PSScriptRoot) 'McClient.uproject'
$Stage = Join-Path $OutDir "stage-$Date"
$Zip = Join-Path $OutDir "McUnreal-$Date.zip"
New-Item -ItemType Directory -Force $OutDir | Out-Null

if (-not $SkipBuild) {
    if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
    & "$EngineRoot\Build\BatchFiles\RunUAT.bat" BuildCookRun "-project=$Project" -noP4 -platform=Win64 `
        -clientconfig=Shipping -target=McClient -build -cook -stage -pak -prereqs -archive "-archivedirectory=$Stage" `
        -utf8output -nocompileeditor
    if ($LASTEXITCODE -ne 0) { throw "BuildCookRun failed ($LASTEXITCODE)" }
}

# Debug symbols are 240 MB and no use to a friend.
Get-ChildItem -Recurse -File $Stage -Filter *.pdb | Remove-Item -Force

# Nothing of Mojang's goes in the package: no cache/ folder, no game assets or data, no jars.
$files = Get-ChildItem -Recurse -File $Stage
$bad = $files | Where-Object {
    $rel = $_.FullName.Substring($Stage.Length)
    $rel -match '(^|[\\/])cache([\\/]|$)' -or $rel -match '(^|[\\/])(assets|data|reports|blockstates)[\\/]minecraft' -or
    $_.Extension -in '.ogg', '.jar', '.mcmeta', '.nbt' -or $_.Name -in 'sounds.json', 'blocks.json', 'registries.json'
}
if ($bad) {
    $bad | ForEach-Object { Write-Error "Mojang content staged: $($_.FullName)" }
    throw 'The stage holds files that must not be shared.'
}

Copy-Item (Join-Path $PSScriptRoot 'package\README.txt') $Stage -Force
Copy-Item (Join-Path $PSScriptRoot 'package\Play.bat') $Stage -Force

$listing = Join-Path $OutDir "McUnreal-$Date-files.txt"
Get-ChildItem -Recurse -File $Stage | ForEach-Object { '{0,14:N0}  {1}' -f $_.Length, $_.FullName.Substring($Stage.Length + 1) } | Set-Content $listing -Encoding utf8
if (Test-Path $Zip) { Remove-Item $Zip }
Compress-Archive -Path (Join-Path $Stage '*') -DestinationPath $Zip -CompressionLevel Optimal
$h = Get-FileHash -Algorithm SHA256 $Zip
"{0}  {1:N0} bytes  sha256 {2}" -f $Zip, (Get-Item $Zip).Length, $h.Hash
"File list: $listing"
