<#
.SYNOPSIS
    Names the frames of the stacks in a -McPerf record: on the 1070, where the build's .pdb is.

.DESCRIPTION
    Reads "X   #n Module+0xRVA" lines from Log and prints each with its function, file and line from Exe's .pdb. Uses the
    engine's own dbghelp, which reads the .pdb format the current linker writes, where Windows' may not.
#>
param(
    [Parameter(Mandatory)][string]$Log,
    [Parameter(Mandatory)][string]$Exe
)
$ErrorActionPreference = 'Stop'
Add-Type @'
using System; using System.Runtime.InteropServices;
public class Sym {
 [DllImport("kernel32.dll", CharSet=CharSet.Ansi)] public static extern IntPtr LoadLibrary(string f);
 [DllImport("dbghelp.dll", SetLastError=true, CharSet=CharSet.Ansi)] public static extern bool SymInitialize(IntPtr p, string path, bool inv);
 [DllImport("dbghelp.dll", SetLastError=true, CharSet=CharSet.Ansi)] public static extern ulong SymLoadModuleEx(IntPtr p, IntPtr f, string img, string mod, ulong b, uint size, IntPtr d, uint fl);
 [DllImport("dbghelp.dll", SetLastError=true, CharSet=CharSet.Ansi)] public static extern bool SymFromAddr(IntPtr p, ulong a, out ulong disp, IntPtr sym);
 [DllImport("dbghelp.dll", SetLastError=true, CharSet=CharSet.Ansi)] public static extern bool SymGetLineFromAddr64(IntPtr p, ulong a, out uint disp, IntPtr line);
 [DllImport("dbghelp.dll")] public static extern uint SymSetOptions(uint o);
 public static string Name(IntPtr p, ulong addr) {
   IntPtr buf = Marshal.AllocHGlobal(88 + 2048);
   for (int i = 0; i < 88 + 2048; i++) Marshal.WriteByte(buf, i, 0);
   Marshal.WriteInt32(buf, 0, 88); Marshal.WriteInt32(buf, 80, 2048);
   ulong d; string r;
   if (SymFromAddr(p, addr, out d, buf)) r = Marshal.PtrToStringAnsi(buf + 84) + "+0x" + d.ToString("x"); else r = "?";
   Marshal.FreeHGlobal(buf);
   IntPtr line = Marshal.AllocHGlobal(40); for (int i = 0; i < 40; i++) Marshal.WriteByte(line, i, 0); Marshal.WriteInt32(line, 0, 40);
   uint ld;
   if (SymGetLineFromAddr64(p, addr, out ld, line)) r += "  " + Marshal.PtrToStringAnsi(Marshal.ReadIntPtr(line, 16)) + ":" + Marshal.ReadInt32(line, 24);
   Marshal.FreeHGlobal(line);
   return r;
 }
}
'@
$Help = 'C:\Program Files\Epic Games\UE_5.9\Engine\Binaries\ThirdParty\DbgHelp\dbghelp.dll'
if (Test-Path $Help) { [Sym]::LoadLibrary($Help) | Out-Null }
$H = [IntPtr]1
[Sym]::SymSetOptions([uint32]0x212) | Out-Null
[Sym]::SymInitialize($H, (Split-Path $Exe), $false) | Out-Null
$Base = [uint64]0x140000000
[Sym]::SymLoadModuleEx($H, [IntPtr]::Zero, $Exe, $null, $Base, 0x0B000000, [IntPtr]::Zero, 0) | Out-Null
$Module = [IO.Path]::GetFileName($Exe)
foreach ($Line in Get-Content $Log) {
    if ($Line -notmatch '^X ') { continue }
    if ($Line -match '^X\s+#(\d+) (\S+)\+0x([0-9a-f]+)') {
        $Name = if ($Matches[2] -eq $Module) { [Sym]::Name($H, $Base + [Convert]::ToUInt64($Matches[3], 16)) } else { '' }
        "{0,3} {1}+0x{2} {3}" -f $Matches[1], $Matches[2], $Matches[3], $Name
    } else { $Line }
}
