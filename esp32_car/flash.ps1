# ============================================================
#  Build + Flash + Monitor firmware ESP32 (PlatformIO CLI)
#  Chay trong PowerShell:  .\flash.ps1
#  Tuy chon:
#     .\flash.ps1 -Port COM5      # chi dinh cong
#     .\flash.ps1 -NoMonitor      # khong mo serial monitor sau khi flash
#     .\flash.ps1 -BuildOnly      # chi build, khong flash
# ============================================================
param(
    [string]$Port = "",
    [string]$Env = "debug",      # debug | release
    [switch]$NoMonitor,
    [switch]$BuildOnly,
    [switch]$Ota                 # flash qua WiFi (khong can USB)
)
if ($Ota) { $Env = "ota"; $NoMonitor = $true }   # env:ota (espota), khong monitor serial

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

# --- Tim lenh pio ---
$pio = (Get-Command pio -ErrorAction SilentlyContinue).Source
if (-not $pio) {
    $fallback = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
    if (Test-Path $fallback) { $pio = $fallback }
}
if (-not $pio) {
    Write-Host "Khong tim thay 'pio'. Cai PlatformIO Core hoac mo PlatformIO Terminal." -ForegroundColor Red
    exit 1
}
Write-Host "PlatformIO: $pio" -ForegroundColor DarkGray

# --- Build ---
Write-Host "`n[1/3] Building (env: $Env)..." -ForegroundColor Cyan
& $pio run -e $Env
if ($LASTEXITCODE -ne 0) { Write-Host "Build that bai." -ForegroundColor Red; exit 1 }

if ($BuildOnly) { Write-Host "`nBuild xong (BuildOnly)." -ForegroundColor Green; exit 0 }

# --- Upload ---
Write-Host "`n[2/3] Uploading (env: $Env)..." -ForegroundColor Cyan
$uploadArgs = @("run", "-e", $Env, "-t", "upload")
if ($Port -ne "") { $uploadArgs += @("--upload-port", $Port) }
& $pio @uploadArgs
if ($LASTEXITCODE -ne 0) { Write-Host "Upload that bai. Thu chi dinh -Port COMx." -ForegroundColor Red; exit 1 }

# --- Monitor ---
if (-not $NoMonitor) {
    Write-Host "`n[3/3] Serial Monitor (115200). Ctrl+C de thoat." -ForegroundColor Cyan
    $monArgs = @("device", "monitor", "-b", "115200")
    if ($Port -ne "") { $monArgs += @("--port", $Port) }
    & $pio @monArgs
} else {
    Write-Host "`nHoan tat (khong mo monitor)." -ForegroundColor Green
}
