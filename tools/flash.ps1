# tools/flash.ps1 — flash + reset the external-microphone validation firmware over the RP debug probe (SWD) with
# OpenOCD. Reliable: no BOOTSEL/picotool race. For the current FLASH binary it programs
# QSPI flash, verifies, and resets to run.
#
# Usage: powershell -File tools/flash.ps1
param(
    [string]$Elf       = "C:/buildfiles/externalmicvalid/externalmicvalid.elf",
    [int]$AdapterKHz   = 5000
)
$ErrorActionPreference = 'Stop'

$PicoRoot   = "$env:USERPROFILE/.pico-sdk"
$OpenOcd    = "$PicoRoot/openocd/0.12.0+dev/openocd.exe"
$OcdScripts = "$PicoRoot/openocd/0.12.0+dev/scripts"
foreach ($p in @($OpenOcd, $OcdScripts)) { if (-not (Test-Path $p)) { throw "Missing: $p" } }
if (-not (Test-Path $Elf)) { throw "ELF not found: $Elf (build first with tools/build.ps1)" }

$elfFwd = $Elf -replace '\\','/'
& $OpenOcd -s $OcdScripts `
    -f "interface/cmsis-dap.cfg" `
    -c "adapter speed $AdapterKHz" `
    -f "target/rp2350.cfg" `
    -c "program {$elfFwd} verify reset exit"
if ($LASTEXITCODE -ne 0) { throw "openocd program failed ($LASTEXITCODE)" }
Write-Host "Flashed + reset via OpenOCD -> $Elf"
