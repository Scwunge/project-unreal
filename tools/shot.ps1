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
$Editor = 'J:\UE_5.9\Engine\Binaries\Win64\UnrealEditor.exe'
$OutPath = if ([IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path (Split-Path $PSScriptRoot -Parent) $Out }
if (Test-Path $OutPath) { Remove-Item $OutPath }
# One build, test or shot at a time across checkouts: an editor started beside another session's build waits for it.
while (Get-Process -Name dotnet, UnrealEditor, UnrealEditor-Cmd -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 10 }
$DelayArg = if ($Args -match "-McShotDelay=") { "" } else { "-McShotDelay=$Delay" }
$All = "`"$Project`" -game -windowed -ResX=1280 -ResY=720 -nosound -log $DelayArg -McShot=`"$OutPath`" $Args"
$Process = Start-Process $Editor $All -PassThru
# -McDesk=<first>,<every>,<count> in Args: also takes pictures of the game's own windows (Saved\Shots\win_N_K.png), only
# those windows and never the rest of the desktop, to see what the game view alone can't show (the loading screen's window).
if ($Args -match '-McDesk=(\d+),(\d+),(\d+)') {
    Add-Type -AssemblyName System.Windows.Forms, System.Drawing
    Add-Type -TypeDefinition @'
using System; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class McWin {
  delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public static List<RECT> Of(int pid) {
    var list = new List<RECT>();
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); RECT r;
      if (p == pid && IsWindowVisible(h) && GetWindowRect(h, out r) && r.R - r.L > 300 && r.B - r.T > 200) list.Add(r); return true; }, IntPtr.Zero);
    return list;
  }
}
'@
    $Dir = Split-Path $OutPath -Parent
    Start-Sleep -Seconds ([int]$Matches[1])
    for ($n = 0; $n -lt [int]$Matches[3] -and -not $Process.HasExited; $n++) {
        $k = 0
        foreach ($r in [McWin]::Of($Process.Id)) {
            $w = $r.R - $r.L; $h = $r.B - $r.T
            $Bitmap = New-Object System.Drawing.Bitmap $w, $h
            [System.Drawing.Graphics]::FromImage($Bitmap).CopyFromScreen($r.L, $r.T, 0, 0, $Bitmap.Size)
            $Bitmap.Save((Join-Path $Dir "win_${n}_$k.png"))
            $Bitmap.Dispose(); $k++
        }
        Start-Sleep -Seconds ([int]$Matches[2])
    }
}
if (-not $Process.WaitForExit($TimeoutSeconds * 1000)) {
    Stop-Process -Id $Process.Id -Force
    Write-Host "Timed out after $TimeoutSeconds s"
}
if (Test-Path $OutPath) { Write-Host "Shot: $OutPath" } else { Write-Host 'No shot was saved'; exit 1 }
