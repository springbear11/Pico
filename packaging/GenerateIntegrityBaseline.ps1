[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$RuntimeDirectory,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $RuntimeDirectory).Path
$path = Join-Path $root 'IntegrityBaseline.json'
if ((Test-Path -LiteralPath $path) -and -not $Force) {
    throw 'IntegrityBaseline.json already exists. Use authorized approval or explicitly pass -Force for a release build.'
}
$names = @('PicoATE.UI.exe', 'PicoATECore.dll')
$plugins = Join-Path $root 'plugins'
if (Test-Path -LiteralPath $plugins) {
    $entries = @((Get-Item -LiteralPath $plugins -Force)) + @(Get-ChildItem -LiteralPath $plugins -Recurse -Force)
    foreach ($entry in $entries) {
        if (($entry.PSIsContainer -or $entry.Name -like 'PicoATE.*.dll') -and
            ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Linked plugin path is not allowed: $($entry.FullName)" }
    }
    $names += @($entries | Where-Object { -not $_.PSIsContainer -and $_.Name -like 'PicoATE.*.dll' } |
        Sort-Object FullName | ForEach-Object { $_.FullName.Substring($root.Length + 1).Replace('\','/') })
}
$files = $names | ForEach-Object {
    $file = Join-Path $root $_
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required file missing: $file" }
    if ((Get-Item -LiteralPath $file -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked component file is not allowed: $file" }
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($file)
    $version = ''
    if ($info.FileVersion) {
        $version = "$($info.FileMajorPart).$($info.FileMinorPart).$($info.FileBuildPart)"
        if ($info.FilePrivatePart) { $version += ".$($info.FilePrivatePart)" }
    }
    if (($_ -in @('PicoATE.UI.exe','PicoATECore.dll') -or (Split-Path $file -Leaf) -like 'PicoATE.*.dll') -and -not $version) {
        throw "Component version resource is missing: $file"
    }
    [ordered]@{path=$_; version=$version; sha256=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$baseline = [ordered]@{
    schemaVersion=2
    algorithm='SHA-256'
    updatedAtUtc=[DateTime]::UtcNow.ToString('o')
    updatedBy='release-build'
    files=@($files)
    history=@()
}
[IO.File]::WriteAllText($path, ($baseline | ConvertTo-Json -Depth 10)+[Environment]::NewLine,
                       [Text.UTF8Encoding]::new($false))
Write-Host "Generated component version and UI/Core/plugin integrity baseline: $path ($($files.Count) files)"
