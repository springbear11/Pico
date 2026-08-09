[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PortableDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Find-DumpBin {
    if ($env:PICOATE_DUMPBIN -and
        (Test-Path -LiteralPath $env:PICOATE_DUMPBIN -PathType Leaf)) {
        return $env:PICOATE_DUMPBIN
    }
    $command = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} `
        'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        return $null
    }
    $installation = & $vswhere -latest -products * -property installationPath
    if (-not $installation) {
        return $null
    }
    return Get-ChildItem -LiteralPath (Join-Path $installation 'VC\Tools\MSVC') `
        -Recurse -Filter dumpbin.exe -File |
        Where-Object { $_.FullName -match '\\Hostx64\\x64\\dumpbin\.exe$' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}

$portable = (Resolve-Path -LiteralPath $PortableDirectory).Path
$errors = [System.Collections.Generic.List[string]]::new()
$requiredFiles = @(
    'PicoATE.UI.exe',
    'PicoATECore.dll',
    'PicoATE.NativeHost.exe',
    'Qt6Core.dll',
    'Qt6Gui.dll',
    'Qt6Widgets.dll',
    'MSVCP140.dll',
    'VCRUNTIME140.dll',
    'VCRUNTIME140_1.dll',
    'platforms\qwindows.dll',
    'plugins\PluginRegistry.json',
    'plugins\PicoATE.CAN.CX.dll',
    'plugins\PicoATE.CAN.GCAN.dll',
    'plugins\PicoATE.DMM.HDM3000.dll',
    'plugins\PicoATE.DMM.KEYSIGHT34410A.dll',
    'plugins\PicoATE.Modbus.Tcp.dll',
    'plugins\PicoATE.PSU.KORAD.dll',
    'plugins\ControlCAN.dll',
    'plugins\ECanVci64.dll',
    'plugins\CHUSBDLL64.dll',
    'plugins\msvcr120.dll',
    'ProductRouting.json'
)
foreach ($relativePath in $requiredFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $portable $relativePath) -PathType Leaf)) {
        $errors.Add("Missing required file: $relativePath")
    }
}
foreach ($relativePath in @('plugins', 'projects', 'image', 'log', 'diagnostics')) {
    if (-not (Test-Path -LiteralPath (Join-Path $portable $relativePath) -PathType Container)) {
        $errors.Add("Missing required directory: $relativePath")
    }
}

$forbidden = @(Get-ChildItem -LiteralPath $portable -Recurse -File |
    Where-Object {
        $_.Extension -in @('.pdb', '.lib', '.exp', '.ilk') -or
        $_.Name -match '^Qt6.+d\.dll$'
    })
foreach ($file in $forbidden) {
    $errors.Add("Debug/development artifact is present: $($file.FullName.Substring($portable.Length + 1))")
}

$registryPath = Join-Path $portable 'plugins\PluginRegistry.json'
if (Test-Path -LiteralPath $registryPath -PathType Leaf) {
    try {
        $registry = Get-Content -LiteralPath $registryPath -Raw | ConvertFrom-Json
        $registered = @($registry.plugins)
        $pluginDlls = @(Get-ChildItem -LiteralPath (Join-Path $portable 'plugins') `
            -Filter 'PicoATE.*.dll' -File)
        if ($registered.Count -ne $pluginDlls.Count) {
            $errors.Add("PluginRegistry has $($registered.Count) entries but $($pluginDlls.Count) plugin DLLs are present")
        }
        foreach ($plugin in $registered) {
            if ([string]::IsNullOrWhiteSpace([string]$plugin.dll) -or
                -not (Test-Path -LiteralPath (Join-Path $portable "plugins\$($plugin.dll)") -PathType Leaf)) {
                $errors.Add("PluginRegistry references a missing DLL: $($plugin.dll)")
            }
        }
    } catch {
        $errors.Add("PluginRegistry.json is invalid: $($_.Exception.Message)")
    }
}

$dumpbin = Find-DumpBin
if (-not $dumpbin) {
    $errors.Add('dumpbin.exe was not found; PE dependency verification could not run')
} else {
    $packageNames = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    Get-ChildItem -LiteralPath $portable -Recurse -File | ForEach-Object {
        [void]$packageNames.Add($_.Name)
    }
    $mustBeLocal = '^(Qt6.*|MSVCP\d+|VCRUNTIME\d+(_\d+)?|CONCRT\d+|MSVCR120|CHUSBDLL64)\.dll$'
    $system32 = Join-Path $env:SystemRoot 'System32'
    $peFiles = @(Get-ChildItem -LiteralPath $portable -Recurse -File |
        Where-Object { $_.Extension -in @('.exe', '.dll') })
    foreach ($pe in $peFiles) {
        $output = & $dumpbin /nologo /dependents $pe.FullName 2>&1
        if ($LASTEXITCODE -ne 0) {
            $errors.Add("dumpbin failed for $($pe.FullName.Substring($portable.Length + 1))")
            continue
        }
        $dependencies = @($output | ForEach-Object {
            if ($_ -match '^\s+([A-Za-z0-9_.+-]+\.dll)\s*$') { $matches[1] }
        } | Where-Object { $_ } | Sort-Object -Unique)
        foreach ($dependency in $dependencies) {
            if ($packageNames.Contains($dependency)) {
                continue
            }
            if ($dependency -match $mustBeLocal) {
                $errors.Add("Missing app-local dependency $dependency required by $($pe.Name)")
                continue
            }
            if ($dependency -like 'api-ms-win-*.dll' -or
                (Test-Path -LiteralPath (Join-Path $system32 $dependency) -PathType Leaf)) {
                continue
            }
            $errors.Add("Unresolved dependency $dependency required by $($pe.Name)")
        }
    }
}

if ($errors.Count -gt 0) {
    throw "Portable verification failed:`n - $($errors -join "`n - ")"
}

$fileCount = @(Get-ChildItem -LiteralPath $portable -Recurse -File).Count
$totalBytes = (Get-ChildItem -LiteralPath $portable -Recurse -File |
    Measure-Object -Property Length -Sum).Sum
Write-Host ("Portable verification passed: {0} files, {1:N1} MB" -f `
    $fileCount, ($totalBytes / 1MB))
