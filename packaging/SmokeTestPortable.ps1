[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PortableDirectory,
    [int]$ObserveMilliseconds = 5000
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$portable = (Resolve-Path -LiteralPath $PortableDirectory).Path
$executable = Join-Path $portable 'PicoATE.UI.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "PicoATE.UI.exe is missing: $executable"
}

$diagnosticDirectory = Join-Path $portable 'diagnostics'
$beforeDumps = @{}
if (Test-Path -LiteralPath $diagnosticDirectory) {
    Get-ChildItem -LiteralPath $diagnosticDirectory -Filter '*.dmp' -File |
        ForEach-Object {
        $beforeDumps[$_.FullName] = $_.LastWriteTimeUtc
    }
}

$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $executable
$startInfo.WorkingDirectory = $portable
$startInfo.UseShellExecute = $false
$startInfo.EnvironmentVariables['PATH'] =
    "$portable;$env:SystemRoot\System32;$env:SystemRoot"
$process = [System.Diagnostics.Process]::new()
$process.StartInfo = $startInfo
if (-not $process.Start()) {
    throw 'Failed to start PicoATE.UI.exe'
}

Start-Sleep -Milliseconds $ObserveMilliseconds
if ($process.HasExited) {
    throw "PicoATE.UI.exe exited during smoke test with code $($process.ExitCode)"
}

[void]$process.CloseMainWindow()
if (-not $process.WaitForExit(5000)) {
    $process.Kill()
    [void]$process.WaitForExit(5000)
    throw 'PicoATE.UI.exe did not close cleanly during the smoke test'
}

$newDumps = @()
if (Test-Path -LiteralPath $diagnosticDirectory) {
    $newDumps = @(Get-ChildItem -LiteralPath $diagnosticDirectory -Filter '*.dmp' -File |
        Where-Object {
            -not $beforeDumps.ContainsKey($_.FullName) -or
            $_.LastWriteTimeUtc -gt $beforeDumps[$_.FullName]
        })
}
if ($newDumps.Count -gt 0) {
    throw "Smoke test produced crash dump(s): $($newDumps.Name -join ', ')"
}

# Each normal launch creates an action trace. Keep crash traces, but do not ship
# the successful packaging smoke-test trace to customers.
if (Test-Path -LiteralPath $diagnosticDirectory) {
    Get-ChildItem -LiteralPath $diagnosticDirectory `
        -Filter "*_$($process.Id).actions.log" -File |
        Remove-Item -Force -ErrorAction SilentlyContinue
}

Write-Host 'Portable smoke test passed with an isolated PATH.'
