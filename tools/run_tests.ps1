<#
.SYNOPSIS
    Builds the McClient editor target, runs the Unreal automation tests and prints a pass/fail summary.

.DESCRIPTION
    1. Builds McClientEditor (Win64 Development), retrying once if the linker fails with LNK1104 (a locked output file).
    2. Runs UnrealEditor-Cmd with "Automation RunTests <Filter>" and writes the engine log to Saved/Logs/run_tests.log.
    3. Reads "Test Completed. Result={..} Name={..} Path={..}" lines from that log and lists the failing tests with
       the "Error: Expected ..." lines reported while they ran.

    4. Checks every test the editor queued (the tab-indented list after "Found N automation tests") completed; a test
       that started but never finished (the editor died or quit early) counts as a failure, not a pass.

    Exits 0 only if the build succeeded, the editor exited cleanly, at least one test ran, every queued test
    completed and none failed.

.PARAMETER Filter
    Automation test filter (default McClient, which matches the McClient.* tests).

.PARAMETER SkipBuild
    Run the tests against the existing binaries.

.PARAMETER FromLog
    Only summarise an existing log (no build, no editor). For checking the parser.
#>
[CmdletBinding()]
param(
    [string]$Filter = 'McClient',
    [switch]$SkipBuild,
    [string]$FromLog
)

$EngineRoot = 'E:\Epic Games\UE_5.8\Engine'
$Project = Join-Path (Split-Path $PSScriptRoot) 'McClient.uproject'
$BuildBat = Join-Path $EngineRoot 'Build\BatchFiles\Build.bat'
$EditorCmd = Join-Path $EngineRoot 'Binaries\Win64\UnrealEditor-Cmd.exe'
$LogPath = Join-Path (Split-Path $Project) 'Saved\Logs\run_tests.log'

# Native tools write progress to stderr; don't let that turn into terminating errors.
$ErrorActionPreference = 'Continue'

function Invoke-Build {
    $buildArgs = @(
        'McClientEditor', 'Win64', 'Development',
        "-Project=$Project", '-WaitMutex', '-NoHotReloadFromIDE'
    )
    for ($attempt = 1; $attempt -le 2; ++$attempt) {
        Write-Host "== Build McClientEditor (attempt $attempt of 2)"
        $lines = New-Object System.Collections.Generic.List[string]
        & $BuildBat @buildArgs 2>&1 | ForEach-Object {
            $text = "$_"
            $lines.Add($text)
            Write-Host $text
        }
        $code = $LASTEXITCODE
        if ($code -eq 0) {
            return 0
        }
        # LNK1104: cannot open the output file, usually an editor or antivirus still holding it. Worth one more go.
        if ($attempt -eq 1 -and ($lines | Where-Object { $_ -match 'LNK1104' })) {
            Write-Host 'Build hit LNK1104; retrying once.'
            Start-Sleep -Seconds 5
            continue
        }
        return $code
    }
}

function Get-TestResults([string]$Path) {
    $results = New-Object System.Collections.Generic.List[object]
    # The editor reports a test's "Error: Expected ..." lines either before or (usually) just after its
    # "Test Completed" line, so lines go to the test completed most recently, until the next one starts.
    $pending = New-Object System.Collections.Generic.List[string]
    $last = $null
    foreach ($line in [System.IO.File]::ReadLines($Path)) {
        if ($line -match 'Test Started\.') {
            $pending.Clear()
            $last = $null
        }
        elseif ($line -match 'Error: Expected') {
            $text = $line.Trim()
            # (Not "$target = if ...": that would unroll an empty list to $null.)
            if ($last) {
                if (-not $last.Errors.Contains($text)) { $last.Errors.Add($text) }
            }
            elseif (-not $pending.Contains($text)) {
                $pending.Add($text)
            }
        }
        elseif ($line -match 'Test Completed\. Result=\{(?<result>[^}]*)\} Name=\{(?<name>[^}]*)\} Path=\{(?<path>[^}]*)\}') {
            $last = [pscustomobject]@{
                Result = $Matches['result']
                Name   = $Matches['name']
                Path   = $Matches['path']
                Errors = New-Object System.Collections.Generic.List[string]
            }
            foreach ($text in $pending) {
                $last.Errors.Add($text)
            }
            $pending.Clear()
            $results.Add($last)
        }
    }
    return $results
}

function Get-QueuedTests([string]$Path) {
    # The editor lists what it is about to run as "LogAutomationCommandLine: Display: <tab><test path>".
    $queued = New-Object System.Collections.Generic.List[string]
    foreach ($line in [System.IO.File]::ReadLines($Path)) {
        if ($line -match 'LogAutomationCommandLine: Display: \t(?<path>\S+)\s*$' -and -not $queued.Contains($Matches['path'])) {
            $queued.Add($Matches['path'])
        }
    }
    return $queued
}

function Write-Summary($Results, $Queued) {
    $passed = @($Results | Where-Object { $_.Result -eq 'Success' })
    $failed = @($Results | Where-Object { $_.Result -ne 'Success' })
    $completed = @($Results | ForEach-Object { $_.Path })
    $unfinished = @($Queued | Where-Object { $completed -notcontains $_ })
    Write-Host ''
    Write-Host '== Test summary'
    Write-Host ("Passed: {0}   Failed: {1}   Did not finish: {2}   Total: {3}" -f $passed.Count, $failed.Count, $unfinished.Count, [Math]::Max(@($Results).Count, @($Queued).Count))
    foreach ($path in $unfinished) {
        Write-Host ''
        Write-Host ("DID NOT FINISH  {0}  (queued, but the log has no result for it)" -f $path)
    }
    foreach ($test in $failed) {
        Write-Host ''
        Write-Host ("FAIL  {0}  ({1})" -f $test.Path, $test.Result)
        if ($test.Errors.Count -eq 0) {
            Write-Host '      (no "Error: Expected" lines found for this test; see the log)'
        }
        foreach ($errorLine in $test.Errors) {
            Write-Host "      $errorLine"
        }
    }
    return $failed.Count + $unfinished.Count
}

if ($FromLog) {
    $results = Get-TestResults $FromLog
    $failedCount = Write-Summary $results (Get-QueuedTests $FromLog)
    if (@($results).Count -eq 0) { exit 2 }
    exit ([int]($failedCount -gt 0))
}

foreach ($tool in @($Project, $EditorCmd)) {
    if (-not (Test-Path -LiteralPath $tool)) {
        Write-Host "Missing: $tool"
        exit 3
    }
}

if (-not $SkipBuild) {
    if (-not (Test-Path -LiteralPath $BuildBat)) {
        Write-Host "Missing: $BuildBat"
        exit 3
    }
    $buildCode = Invoke-Build
    if ($buildCode -ne 0) {
        Write-Host ''
        Write-Host "BUILD FAILED (exit code $buildCode)"
        exit $buildCode
    }
}

New-Item -ItemType Directory -Force -Path (Split-Path $LogPath) | Out-Null
if (Test-Path -LiteralPath $LogPath) {
    Remove-Item -LiteralPath $LogPath -Force
}

Write-Host "== Run automation tests: $Filter"
# SoftQuit ends the editor through a normal engine exit once the queue is done. (-TestExit forces the process down
# straight away, which can cut the last lines, the results of the last tests, off the log.)
& $EditorCmd $Project "-ExecCmds=Automation RunTests $Filter;SoftQuit" `
    -unattended -nopause -nosplash -NullRHI "-abslog=$LogPath"
$editorCode = $LASTEXITCODE

if (-not (Test-Path -LiteralPath $LogPath)) {
    Write-Host "No log was written to $LogPath (editor exit code $editorCode)."
    exit 4
}

$results = Get-TestResults $LogPath
$failedCount = Write-Summary $results (Get-QueuedTests $LogPath)
Write-Host ''
Write-Host "Log: $LogPath"

if (@($results).Count -eq 0) {
    Write-Host "No tests ran for filter '$Filter' (editor exit code $editorCode)."
    exit 2
}
if ($failedCount -gt 0) {
    exit 1
}
if ($editorCode -ne 0) {
    Write-Host "All tests passed but the editor exited with code $editorCode."
    exit $editorCode
}
Write-Host 'ALL TESTS PASSED'
exit 0
