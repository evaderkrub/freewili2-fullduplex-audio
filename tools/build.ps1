# tools/build.ps1 — configure + build the external-microphone validation firmware using the
# Raspberry Pi Pico VS Code extension's installed toolchain. No global env changes:
# all paths are set for THIS process only.
#
# Usage:
#   powershell -File tools/build.ps1            # incremental build
#   powershell -File tools/build.ps1 -Clean     # wipe build dir first
param(
    [string]$BuildDir = "C:/buildfiles/externalmicvalid",
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'

# --- Locate the extension-installed toolchain (versions discovered 2026-06) ---
$PicoRoot    = "$env:USERPROFILE/.pico-sdk"
$SdkPath     = "$PicoRoot/sdk/2.2.0"
$Toolchain   = "$PicoRoot/toolchain/14_2_Rel1"
$NinjaDir    = "$PicoRoot/ninja/v1.12.1"
$CMakeBin    = "$PicoRoot/cmake/v3.31.5/bin"
$PicotoolDir = "$PicoRoot/picotool/2.2.0-a4/picotool"

foreach ($p in @($SdkPath, $Toolchain, $NinjaDir, $CMakeBin, $PicotoolDir)) {
    if (-not (Test-Path $p)) { throw "Missing Pico tool path: $p (check .pico-sdk versions)" }
}

# Repo root is the parent of this tools/ dir.
$RepoRoot  = Split-Path -Parent $PSScriptRoot
$BoardDirs = "$RepoRoot/src/boards"

# Prepend the extension's cmake/ninja/toolchain/picotool to PATH for this process.
$env:Path = "$CMakeBin;$NinjaDir;$Toolchain/bin;$PicotoolDir;$env:Path"
$env:PICO_SDK_PATH = $SdkPath
$env:PICO_TOOLCHAIN_PATH = $Toolchain

if ($Clean -and (Test-Path $BuildDir)) { Remove-Item -Recurse -Force $BuildDir }

cmake -G Ninja -B $BuildDir -S $RepoRoot `
    -DCMAKE_BUILD_TYPE=Release `
    -DPICO_SDK_PATH="$SdkPath" `
    -DPICO_TOOLCHAIN_PATH="$Toolchain" `
    -DPICO_PLATFORM=rp2350 `
    -Dpicotool_DIR="$PicotoolDir"
    # NOTE: do NOT pass -DPICO_BOARD here — CMakeLists.txt sets PICO_BOARD=freewili2
    # (RP2350B). Passing pico2 would override it back to the wrong RP2350A config.
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }

cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) { throw "cmake build failed ($LASTEXITCODE)" }

Write-Host "Build OK -> $BuildDir/externalmicvalid.uf2"
