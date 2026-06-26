# Mo Serial Monitor (115200). Ctrl+C de thoat.
#   .\monitor.ps1            # auto-detect cong
#   .\monitor.ps1 -Port COM5 # chi dinh cong
param([string]$Port = "")

Set-Location $PSScriptRoot
$pio = (Get-Command pio -ErrorAction SilentlyContinue).Source
if (-not $pio) {
    $f = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
    if (Test-Path $f) { $pio = $f }
}
if (-not $pio) { Write-Host "Khong tim thay 'pio'." -ForegroundColor Red; exit 1 }

$args = @("device", "monitor", "-b", "115200")
if ($Port -ne "") { $args += @("--port", $Port) }
& $pio @args
