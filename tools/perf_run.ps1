<#
.SYNOPSIS
    On the 1070's desktop: plays one scripted session of the staged game and samples what only the outside can see.

.DESCRIPTION
    Starts <Stage>\Windows\McClient\Binaries\Win64\McClient-Win64-Shipping.exe with -McPerf and -McInputFile, then every two
    seconds writes the card's memory and load, the process's memory, and the machine's free memory to samples.csv until the game
    exits or Minutes run out. Writes machine.txt (what else was running at the start) and end.txt (how it ended). Stops only
    the process it started.
#>
param(
    [Parameter(Mandatory)][string]$Stage,
    [Parameter(Mandatory)][string]$Dir,
    [int]$Minutes = 8
)
$Exe = Join-Path $Stage 'Windows\McClient\Binaries\Win64\McClient-Win64-Shipping.exe'
$Extra = if (Test-Path "$Dir\args.txt") { (Get-Content "$Dir\args.txt" -Raw) } else { '' }
if ($null -eq $Extra) { $Extra = '' }
Remove-Item "$Dir\perf.log", "$Dir\samples.csv", "$Dir\end.txt" -ErrorAction SilentlyContinue

function Snapshot {
    $o = Get-CimInstance Win32_OperatingSystem
    $cpu = (Get-CimInstance Win32_Processor | Measure-Object LoadPercentage -Average).Average
    $g = (& nvidia-smi --query-gpu=memory.used,utilization.gpu --format=csv,noheader,nounits) -replace ' ', ''
    "{0},{1},{2}" -f [int]($o.FreePhysicalMemory / 1024), $cpu, $g
}

$Before = @(
    "start $(Get-Date -Format o)"
    "free_mb,cpu,vram_mb,gpu_pct: $(Snapshot)"
    Get-Process | Where-Object { $_.WS -gt 200MB -or $_.Name -match 'Unreal|java|McClient|Cook|dotnet|cl$|link' } | Sort-Object WS -Descending |
        ForEach-Object { "{0} {1} {2} MB" -f $_.Name, $_.Id, [int]($_.WS / 1MB) }
)
$Before | Set-Content "$Dir\machine.txt"

$GameArgs = "-McPerf=`"$Dir\perf.log`" -McInputFile=`"$Dir\script.txt`" -McCache=C:\mc-runs\mc-unreal\cache $Extra"
$P = Start-Process $Exe -ArgumentList $GameArgs -PassThru
"pid $($P.Id) args $GameArgs"
'secs,free_mb,cpu,vram_mb,gpu_pct,game_ws_mb,game_private_mb' | Set-Content "$Dir\samples.csv"
$Began = Get-Date
$Limit = $Began.AddMinutes($Minutes)
$Peak = 0
while (-not $P.HasExited -and (Get-Date) -lt $Limit) {
    Start-Sleep -Seconds 2
    try {
        $P.Refresh()
        $Ws = [int]($P.WorkingSet64 / 1MB)
        if ($Ws -gt $Peak) { $Peak = $Ws }
        "{0:0},{1},{2},{3}" -f ((Get-Date) - $Began).TotalSeconds, (Snapshot), $Ws, [int]($P.PrivateMemorySize64 / 1MB) | Add-Content "$Dir\samples.csv"
    } catch { }
}
if ($P.HasExited) {
    "exit code $($P.ExitCode) after $([int]((Get-Date) - $Began).TotalSeconds) s, peak ws $Peak MB" | Set-Content "$Dir\end.txt"
} else {
    "time limit: stopped after $Minutes min, peak ws $Peak MB" | Set-Content "$Dir\end.txt"
    Stop-Process -Id $P.Id -Force
}
