[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PortableDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$portable = (Resolve-Path -LiteralPath $PortableDirectory).Path
$nativeHost = Join-Path $portable 'PicoATE.NativeHost.exe'
$pluginDirectory = Join-Path $portable 'plugins'
if (-not (Test-Path -LiteralPath $nativeHost -PathType Leaf)) {
    throw "NativeHost is missing: $nativeHost"
}
if (-not (Test-Path -LiteralPath $pluginDirectory -PathType Container)) {
    throw "Plugin directory is missing: $pluginDirectory"
}

$pluginDlls = @(Get-ChildItem -LiteralPath $pluginDirectory -Filter 'PicoATE.*.dll' -File |
    Sort-Object Name)
if ($pluginDlls.Count -eq 0) {
    throw "No PicoATE plugin DLL was found in $pluginDirectory"
}

$plugins = [System.Collections.Generic.List[object]]::new()
$moduleIds = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)

function Get-ModuleIdFromDllName {
    param([Parameter(Mandatory = $true)][string]$DllName)

    $name = [System.IO.Path]::GetFileNameWithoutExtension($DllName).Trim().ToLowerInvariant()
    if ($name.StartsWith('picoate.')) {
        $name = $name.Substring('picoate.'.Length)
    }
    $name = [System.Text.RegularExpressions.Regex]::Replace($name, '[^a-z0-9]+', '.')
    $name = $name.Trim('.')
    if ([string]::IsNullOrWhiteSpace($name)) {
        return ''
    }
    return "plugin.$name"
}

foreach ($dll in $pluginDlls) {
    $stderrPath = [System.IO.Path]::GetTempFileName()
    try {
        $stdout = & $nativeHost --describe --dll $dll.FullName 2> $stderrPath
        $exitCode = $LASTEXITCODE
        $stderrContent = Get-Content -LiteralPath $stderrPath -Raw `
            -ErrorAction SilentlyContinue
        $stderr = if ($null -eq $stderrContent) {
            ''
        } else {
            $stderrContent.ToString().Trim()
        }
        if ($exitCode -ne 0) {
            throw "Describe failed for $($dll.Name) (exit $exitCode): $stderr"
        }
        $json = if ($null -eq $stdout) {
            ''
        } else {
            ($stdout -join [Environment]::NewLine).Trim()
        }
        try {
            $description = $json | ConvertFrom-Json
        } catch {
            throw "Describe returned invalid JSON for $($dll.Name): $json"
        }
        $moduleId = ''
        $topLevelModuleId = $description.PSObject.Properties['moduleId']
        if ($null -ne $topLevelModuleId) {
            $moduleId = [string]$topLevelModuleId.Value
        }
        $descriptionProperty = $description.PSObject.Properties['description']
        if ([string]::IsNullOrWhiteSpace($moduleId) -and
            $null -ne $descriptionProperty -and
            $null -ne $descriptionProperty.Value) {
            $nestedModuleId = $descriptionProperty.Value.PSObject.Properties['moduleId']
            if ($null -ne $nestedModuleId) {
                $moduleId = [string]$nestedModuleId.Value
            }
        }
        if ([string]::IsNullOrWhiteSpace($moduleId)) {
            # Keep the package generator aligned with PluginCatalog/Core fallback rules.
            $moduleId = Get-ModuleIdFromDllName -DllName $dll.Name
        }
        if ([string]::IsNullOrWhiteSpace($moduleId)) {
            throw "No stable moduleId could be resolved for $($dll.Name)"
        }
        if (-not $moduleIds.Add($moduleId)) {
            throw "Duplicate moduleId '$moduleId' returned by $($dll.Name)"
        }
        $description | Add-Member -NotePropertyName moduleId -NotePropertyValue $moduleId -Force
        $description | Add-Member -NotePropertyName dll -NotePropertyValue $dll.Name -Force
        $plugins.Add($description)
    } finally {
        Remove-Item -LiteralPath $stderrPath -Force -ErrorAction SilentlyContinue
    }
}

$registry = [ordered]@{
    plugins = @($plugins)
}
$registryPath = Join-Path $pluginDirectory 'PluginRegistry.json'
$registryJson = $registry | ConvertTo-Json -Depth 100
[System.IO.File]::WriteAllText(
    $registryPath,
    $registryJson + [Environment]::NewLine,
    [System.Text.UTF8Encoding]::new($false))

Write-Host "Generated PluginRegistry.json with $($plugins.Count) plugin(s): $registryPath"
