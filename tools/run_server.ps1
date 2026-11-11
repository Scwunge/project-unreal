<#
.SYNOPSIS
    Runs the dedicated server: the game without a window, for friends to join by address.

.DESCRIPTION
    Settings are in Saved/Server (server.properties, allowlist.json, ops.json), written with the defaults on the
    first start; the world is Saved/Worlds/<level-name>. Stop the server with Ctrl+C.

    It runs on the editor's binaries (-server): an engine installed from the launcher can't build the
    McClientServer target, which needs an engine built from source.

.EXAMPLE
    tools/run_server.ps1
    tools/run_server.ps1 -Port 25570
#>
[CmdletBinding()]
param(
    # Overrides server.properties' server-port.
    [int]$Port = 0
)

$EngineRoot = 'J:\UE_5.9\Engine'
$Project = Join-Path (Split-Path $PSScriptRoot) 'McClient.uproject'
$Editor = Join-Path $EngineRoot 'Binaries\Win64\UnrealEditor.exe'

$serverArgs = @($Project, '-server', '-log', '-nosound', '-unattended')
if ($Port -gt 0) {
    $serverArgs += "-Port=$Port"
}
& $Editor @serverArgs
