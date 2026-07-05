$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $root
try {
    cmake --preset vs2022
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed with exit code $LASTEXITCODE"
    }

    $generated = Join-Path $root '.build\vs2022\PicoATE.Plugins.sln'
    $destination = Join-Path $root 'PicoATE.Plugins.sln'
    $content = Get-Content -LiteralPath $generated -Raw
    $content = [regex]::Replace(
        $content,
        '"([^"\\/:]+\.vcxproj)"',
        '".build\vs2022\$1"')
    Set-Content -LiteralPath $destination -Value $content -Encoding UTF8
    Write-Host "Generated: $destination"
} finally {
    Pop-Location
}
