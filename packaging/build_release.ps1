[CmdletBinding()]
param(
    [string]$Version = '0.2.0',
    [string]$RuntimeSource = '',
    [switch]$SkipTests,
    [switch]$SkipInstaller
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($RuntimeSource)) {
    $RuntimeSource = Join-Path $PSScriptRoot 'runtime'
}
$RuntimeSource = (Resolve-Path -LiteralPath $RuntimeSource).Path
$portableDirectory = Join-Path $repoRoot `
    'out\build\vs2022-qt6-all\portable\Release\PicoATE.UI'
$packageDirectory = Join-Path $repoRoot 'out\package'
$installerDirectory = Join-Path $repoRoot 'out\installer'

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$Program,
        [Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments
    )
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed with exit code $LASTEXITCODE"
    }
}

Push-Location $repoRoot
try {
    Invoke-Checked cmake --preset vs2022-qt6-all

    Push-Location (Join-Path $repoRoot 'templates')
    try {
        Invoke-Checked cmake --preset vs2022
        Invoke-Checked cmake --build --preset vs2022-release
    } finally {
        Pop-Location
    }

    Invoke-Checked cmake --build --preset vs2022-qt6-all-release
    if (-not $SkipTests) {
        Invoke-Checked ctest --preset vs2022-qt6-all-release --output-on-failure
    }
    Invoke-Checked cmake --build --preset vs2022-qt6-all-release `
        --target PicoATEUiPortable

    $defaultRuntime = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot 'runtime')).Path
    if (-not $RuntimeSource.Equals($defaultRuntime, [StringComparison]::OrdinalIgnoreCase)) {
        $destination = Join-Path $portableDirectory 'ProductRouting.json'
        Remove-Item -LiteralPath $destination -Force -ErrorAction SilentlyContinue
        $source = Join-Path $RuntimeSource 'ProductRouting.json'
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            Copy-Item -LiteralPath $source -Destination $destination -Force
        }
    }

    # Product projects are deployed separately from the framework release.
    $portableProjects = Join-Path $portableDirectory 'projects'
    Remove-Item -LiteralPath $portableProjects -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path $portableProjects -Force | Out-Null

    & (Join-Path $PSScriptRoot 'VerifyPortable.ps1') `
        -PortableDirectory $portableDirectory
    if ($LASTEXITCODE -ne 0) {
        throw 'Portable verification failed'
    }
    & (Join-Path $PSScriptRoot 'SmokeTestPortable.ps1') `
        -PortableDirectory $portableDirectory
    if ($LASTEXITCODE -ne 0) {
        throw 'Portable smoke test failed'
    }

    $manifestPath = Join-Path $portableDirectory 'package-manifest.json'
    $files = @(Get-ChildItem -LiteralPath $portableDirectory -Recurse -File |
        Where-Object { $_.FullName -ne $manifestPath } |
        Sort-Object FullName |
        ForEach-Object {
            [ordered]@{
                path = $_.FullName.Substring($portableDirectory.Length + 1).Replace('\', '/')
                size = $_.Length
                sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
            }
        })
    $manifest = [ordered]@{
        product = 'PicoATE'
        version = $Version
        architecture = 'x64'
        builtAtUtc = [DateTime]::UtcNow.ToString('o')
        files = $files
    }
    [IO.File]::WriteAllText(
        $manifestPath,
        ($manifest | ConvertTo-Json -Depth 10) + [Environment]::NewLine,
        [Text.UTF8Encoding]::new($false))

    New-Item -ItemType Directory -Path $packageDirectory -Force | Out-Null
    $zipPath = Join-Path $packageDirectory "PicoATE-Portable-$Version-x64.zip"
    Remove-Item -LiteralPath $zipPath -Force -ErrorAction SilentlyContinue
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory(
        $portableDirectory,
        $zipPath,
        [IO.Compression.CompressionLevel]::Optimal,
        $false)
    Write-Host "Portable ZIP: $zipPath"

    if (-not $SkipInstaller) {
        $isccCandidates = @(
            $env:PICOATE_ISCC,
            (Join-Path $env:ProgramFiles 'Inno Setup 7\ISCC.exe'),
            (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 7\ISCC.exe'),
            (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 7\ISCC.exe')
        ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) }
        $iscc = $isccCandidates | Select-Object -First 1
        if (-not $iscc) {
            $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
            if ($command) {
                $iscc = $command.Source
            }
        }
        if (-not $iscc) {
            throw 'Inno Setup 7 ISCC.exe was not found. Install Inno Setup 7 or use -SkipInstaller.'
        }
        New-Item -ItemType Directory -Path $installerDirectory -Force | Out-Null
        Invoke-Checked $iscc "/DMyAppVersion=$Version" `
            "/DSourceDir=$portableDirectory" `
            "/DOutputDir=$installerDirectory" `
            (Join-Path $PSScriptRoot 'PicoATE.iss')
        Write-Host "Installer directory: $installerDirectory"
    }
} finally {
    Pop-Location
}
