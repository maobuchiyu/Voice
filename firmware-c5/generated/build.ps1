param(
    [ValidateSet('Debug', 'Release')]
    [string]$Profile = 'Debug',
    [switch]$Regenerate,
    [string]$CubeExe = 'E:\STM32cubeMX2\resources\cube-wrapper\0.10.3\bin\cube.exe',
    [string]$CltRoot = 'E:\STM32CubeCLT_1.21.0'
)

$ErrorActionPreference = 'Stop'
$generated = $PSScriptRoot
$projectRoot = Split-Path -Parent $generated
$ioc = Join-Path $projectRoot 'AcousticLab_C5.ioc2'
$mainPath = Join-Path $generated 'main.c'
$cmakePath = Join-Path $generated 'CMakeLists.txt'

foreach ($required in @($CubeExe, $ioc, $mainPath, $cmakePath, (Join-Path $CltRoot 'GNU-tools-for-STM32\bin\arm-none-eabi-gcc.exe'), (Join-Path $CltRoot 'Ninja\bin\ninja.exe'))) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Missing required file: $required"
    }
}

$env:PATH = (Join-Path $CltRoot 'GNU-tools-for-STM32\bin') + ';' + (Join-Path $CltRoot 'Ninja\bin') + ';' + $env:PATH

if ($Regenerate) {
    # CubeMX2 replaces these two user-edited files even with --diff keep_user.
    $userMain = Get-Content -LiteralPath $mainPath -Raw
    $userCMake = Get-Content -LiteralPath $cmakePath -Raw
    $target = if ($Profile -eq 'Debug') { '.debug_GCC+STM32C532CCT6' } else { '.release_GCC+STM32C532CCT6' }
    & $CubeExe mx ide-project generate -p $ioc --format CMake --build-target $target --source include-packs-from-local --destination $generated --force --diff keep_user
    if ($LASTEXITCODE -ne 0) { throw 'CubeMX2 generation failed' }
    Set-Content -LiteralPath $mainPath -Value $userMain -NoNewline
    Set-Content -LiteralPath $cmakePath -Value $userCMake -NoNewline
}

$presets = (Get-Content -LiteralPath (Join-Path $generated 'CMakePresets.json') -Raw | ConvertFrom-Json).configurePresets
# CubeMX2 重新生成后恢复自写板级模块所需的看门狗及 4 KiB 栈配置。
$halConfigPath = Join-Path $generated 'generated\hal\stm32c5xx_hal_conf.h'
$halConfig = Get-Content -LiteralPath $halConfigPath -Raw
$halConfig = $halConfig -replace '#define USE_HAL_IWDG_MODULE\s+[01]U', '#define USE_HAL_IWDG_MODULE 1U'
Set-Content -LiteralPath $halConfigPath -Value $halConfig -Encoding UTF8 -NoNewline
$linkerPath = Join-Path $generated 'user_modifiable\Device\STM32C532CCT6\stm32c532xc_flash.ld'
$linker = (Get-Content -LiteralPath $linkerPath -Raw) -replace 'STACK_SIZE = 0x[0-9a-fA-F]+;', 'STACK_SIZE = 0x1000;'
Set-Content -LiteralPath $linkerPath -Value $linker -Encoding UTF8 -NoNewline
$preset = $presets | Where-Object { -not $_.hidden -and $_.name -match "(?i)^$Profile" -and $_.name -match 'GCC' } | Select-Object -First 1 -ExpandProperty name
if (-not $preset) { throw "$Profile GCC preset is not generated in $generated" }

Push-Location $generated
try {
    & $CubeExe cmake --preset $preset
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
    & $CubeExe cmake --build --preset $preset --parallel 8
    if ($LASTEXITCODE -ne 0) { throw 'GCC build failed' }
} finally {
    Pop-Location
}
