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
$files = @('PicoATE.UI.exe', 'PicoATECore.dll') | ForEach-Object {
    $file = Join-Path $root $_
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required file missing: $file" }
    [ordered]@{path=$_; sha256=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$baseline = [ordered]@{
    schemaVersion=1
    algorithm='SHA-256'
    updatedAtUtc=[DateTime]::UtcNow.ToString('o')
    updatedBy='release-build'
    files=@($files)
    history=@()
}
[IO.File]::WriteAllText($path, ($baseline | ConvertTo-Json -Depth 10)+[Environment]::NewLine,
                       [Text.UTF8Encoding]::new($false))
Write-Host "Generated UI/Core integrity baseline: $path"
