# tools/rtt.ps1 — live RTT console over the CMSIS-DAP probe via OpenOCD.
# Starts an attached (no-flash) OpenOCD with an RTT server on TCP 9090 and
# bridges it to this console. Ctrl+C to stop (kills the OpenOCD it spawned).
#
# Usage: powershell -File tools/rtt.ps1 [-Seconds 0]   # 0 = run until Ctrl+C
param(
    [int]$Port = 9090,
    [int]$Seconds = 0,       # >0: capture for N seconds then exit (for scripted checks)
    [int]$AdapterKHz = 5000
)
$ErrorActionPreference = 'Stop'

$PicoRoot   = "$env:USERPROFILE/.pico-sdk"
$OpenOcd    = "$PicoRoot/openocd/0.12.0+dev/openocd.exe"
$OcdScripts = "$PicoRoot/openocd/0.12.0+dev/scripts"
foreach ($p in @($OpenOcd, $OcdScripts)) { if (-not (Test-Path $p)) { throw "Missing: $p" } }

# RP2350 SRAM is 0x20000000..0x20082000; the RTT control block lives somewhere in it.
# Build a single argument string — PowerShell 5.1 Start-Process re-splits arrays on spaces,
# so -c arguments with spaces (e.g. "adapter speed 5000") must be individually quoted.
$ocdArgs = "-s `"$OcdScripts`" " +
           "-f interface/cmsis-dap.cfg " +
           "-c `"adapter speed $AdapterKHz`" " +
           "-f target/rp2350.cfg " +
           "-c init " +
           "-c `"rtt setup 0x20000000 0x82000 {SEGGER RTT}`" " +
           "-c `"rtt start`" " +
           "-c `"rtt server start $Port 0`""
$ocd = Start-Process -FilePath $OpenOcd -ArgumentList $ocdArgs -PassThru -WindowStyle Hidden
try {
    Start-Sleep -Seconds 2   # let openocd attach and find the control block
    if ($ocd.HasExited) { throw "openocd exited early (code $($ocd.ExitCode)) - is the probe connected?" }
    try {
        $client = New-Object System.Net.Sockets.TcpClient('127.0.0.1', $Port)
    } catch {
        throw "Could not connect to RTT server on port $Port (openocd attached but server not up?): $_"
    }
    $stream = $client.GetStream()
    $buf = New-Object byte[] 4096
    $deadline = if ($Seconds -gt 0) { (Get-Date).AddSeconds($Seconds) } else { [datetime]::MaxValue }
    Write-Host "--- RTT connected (port $Port) ---"
    while ((Get-Date) -lt $deadline) {
        if ($stream.DataAvailable) {
            $n = $stream.Read($buf, 0, $buf.Length)
            if ($n -le 0) { break }
            Write-Host -NoNewline ([System.Text.Encoding]::ASCII.GetString($buf, 0, $n))
        } else {
            Start-Sleep -Milliseconds 50
        }
    }
} finally {
    if ($stream) { $stream.Close() }
    if ($client) { $client.Close() }
    if (-not $ocd.HasExited) { Stop-Process -Id $ocd.Id -Force }
}
Write-Host "`n--- RTT closed ---"
