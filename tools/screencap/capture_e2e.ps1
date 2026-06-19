# tools/screencap/capture_e2e.ps1 — eMeet E2E capture: record room audio across a
# full SILENCE->TONE->SILENCE cycle and grab LCD stills, for hands-free proof that
# the board's speaker emits sound while the duplex firmware runs.
#
# Usage: powershell -File tools/screencap/capture_e2e.ps1 [-Seconds 16]
param(
    [string]$Out = "tools/screencap/captures",
    [int]$Seconds = 16
)
$ErrorActionPreference = 'Stop'
$ff  = "ffmpeg"
$cam = 'video=EMEET SmartCam C960 4K'
$mic = 'audio=Microphone (EMEET SmartCam C960 4K)'
New-Item -ItemType Directory -Force $Out | Out-Null

# Room audio (mono 16 kHz is plenty to resolve a 1 kHz tone). Covers >=1 TONE window.
Write-Host "Recording $Seconds s of eMeet room audio..."
& $ff -hide_banner -loglevel error -f dshow -i $mic -t $Seconds -ac 1 -ar 16000 -y "$Out/room.wav"
Write-Host "Saved $Out/room.wav"

# LCD stills with contrast boost + hflip (the eMeet mirrors horizontally, so hflip
# makes the on-screen banner text readable). Raw 1080p grab is washed out without eq.
# Spaced ~1.5 s over the ~9 s SILENCE/TONE cycle so some land in a TONE (green) window.
1..7 | ForEach-Object {
    & $ff -hide_banner -loglevel error -f dshow -video_size 1920x1080 -i $cam `
         -vf "hflip,eq=contrast=1.5:brightness=-0.03:saturation=1.2" -frames:v 1 -update 1 -y "$Out/lcd_$_.png"
    Start-Sleep -Milliseconds 1500
}
Write-Host "Saved LCD stills $Out/lcd_1..7.png"
