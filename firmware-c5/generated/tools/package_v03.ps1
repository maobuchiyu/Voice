# 将已验证的优化固件固定到v0.3目录；不连接或烧录开发板。
$ErrorActionPreference = 'Stop'
$generatedRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $generatedRoot 'build\release_GCC_STM32C532CCT6'
$releaseRoot = Join-Path $generatedRoot 'release\v0.3.0'
New-Item -ItemType Directory -Path $releaseRoot -Force | Out-Null
foreach ($extension in @('elf', 'hex', 'bin', 'map')) {
    $sourcePath = Join-Path $buildRoot "AcousticLab_C5.$extension"
    if (-not (Test-Path -LiteralPath $sourcePath)) { throw "Release file missing: $sourcePath" }
    Copy-Item -LiteralPath $sourcePath -Destination $releaseRoot -Force
}
foreach ($document in @('PROTOCOL.md','VALIDATION.md')) {
    Copy-Item -LiteralPath (Join-Path $generatedRoot $document) -Destination $releaseRoot -Force
}
$hashLines = foreach ($releaseFile in Get-ChildItem -LiteralPath $releaseRoot -File | Where-Object Name -ne 'SHA256SUMS.txt' | Sort-Object Name) {
    $hash = (Get-FileHash -LiteralPath $releaseFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $($releaseFile.Name)"
}
Set-Content -LiteralPath (Join-Path $releaseRoot 'SHA256SUMS.txt') -Value $hashLines -Encoding ascii
Write-Output "v0.3.0 release prepared: $releaseRoot"
