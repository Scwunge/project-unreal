<#
.SYNOPSIS
    Takes one screenshot of the game with the given switches, waits for it, and prints where it went.

.DESCRIPTION
    Starts the editor in -game mode at 1280x720 with -McShot=<Out> and -McShotDelay=$Delay, passes Args through, and waits
    up to TimeoutSeconds for the process to exit. Run only one build, test or shot at a time.

.EXAMPLE
    tools\shot.ps1 -Out Saved\Shots\hand.png -Args '-McFlat -McWorld=handshot "-McChat=/give @s minecraft:diamond_sword"'
#>
param(
    [Parameter(Mandatory)][string]$Out,
    [string]$Args = '',
    [int]$TimeoutSeconds = 300,
    [double]$Delay = 12
)

$Project = Join-Path (Split-Path $PSScriptRoot -Parent) 'McClient.uproject'
$Editor = 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$OutPath = if ([IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path (Split-Path $PSScriptRoot -Parent) $Out }
if (Test-Path $OutPath) { Remove-Item $OutPath }
# One build, test or shot at a time across checkouts: an editor started beside another session's build waits for it.
while (Get-Process -Name dotnet, UnrealEditor, UnrealEditor-Cmd -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 10 }
$All = "`"$Project`" -game -windowed -ResX=1280 -ResY=720 -nosound -log -McShotDelay=$Delay -McShot=`"$OutPath`" $Args"
$Process = Start-Process $Editor $All -PassThru
if (-not $Process.WaitForExit($TimeoutSeconds * 1000)) {
    Stop-Process -Id $Process.Id -Force
    Write-Host "Timed out after $TimeoutSeconds s"
}
if (Test-Path $OutPath) { Write-Host "Shot: $OutPath" } else { Write-Host 'No shot was saved'; exit 1 }
