# ============================================================
#  Tu hieu chuan CHIEU MOTOR qua Serial
#  - Chay test tung banh, doc encoder, xac dinh chieu
#  - Quy uoc: lenh TIEN  =>  encoder dem DUONG (xe tien thi banh dem +)
#  - Tu dao chieu (iA/iB) cho dung, roi ghi INVERT_A/INVERT_B vao main.cpp
#
#  Cach dung:
#     .\calibrate.ps1                 # auto-detect cong COM
#     .\calibrate.ps1 -Port COM5
#
#  LUU Y: dong Serial Monitor truoc khi chay (cong khong duoc bi chiem).
#         Ke xe len gia / nhac banh khoi mat dat khi test.
# ============================================================
param(
    [string]$Port = "",
    [int]$RunMs = 1500,        # thoi gian chay moi banh (ms)
    [int]$Speed = 200,
    [switch]$NoWrite           # khong ghi vao main.cpp, chi bao ket qua
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

# ---------- Tim cong ----------
if ($Port -eq "") {
    $dev = Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
           Where-Object { $_.Name -match '\(COM\d+\)' -and $_.Name -match 'CP210|CH340|CH910|UART|USB|Silicon' } |
           Select-Object -First 1
    if ($dev -and $dev.Name -match '\((COM\d+)\)') {
        $Port = $Matches[1]
        Write-Host "Auto-detect ESP32: $Port  ($($dev.Name))" -ForegroundColor DarkGray
    } else {
        $ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
        if ($ports.Count -eq 0) { Write-Host "Khong thay cong COM nao." -ForegroundColor Red; exit 1 }
        $Port = $ports[-1]
        Write-Host "Khong nhan dien USB-UART, dung $Port" -ForegroundColor Yellow
    }
}

# ---------- Dong tien trinh giu cong ----------
Get-Process pio,python -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 400

# ---------- Mo cong ----------
$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, None, 8, one
$sp.DtrEnable = $false   # tranh reset ESP32 khi mo cong
$sp.RtsEnable = $false
$sp.ReadTimeout = 200
$sp.NewLine = "`n"
try { $sp.Open() } catch { Write-Host "Khong mo duoc $Port : $_" -ForegroundColor Red; exit 1 }
Start-Sleep -Milliseconds 500
$sp.DiscardInBuffer()
Write-Host "Da mo $Port @115200`n" -ForegroundColor Green

# ---------- Ham tien ich ----------
function Send-Cmd([string]$c) { $sp.WriteLine($c); Start-Sleep -Milliseconds 120 }

# Chay 1 lenh trong $ms, doc tat ca dong, tra ve [hashtable]@{L=..;R=..} (gia tri encoder cuoi)
function Read-Encoder([string]$cmd, [int]$ms) {
    $sp.DiscardInBuffer()
    Send-Cmd "e"          # reset encoder
    Start-Sleep -Milliseconds 200
    $sp.DiscardInBuffer()
    Send-Cmd $cmd         # chay banh
    $end = (Get-Date).AddMilliseconds($ms)
    $L = 0; $R = 0
    while ((Get-Date) -lt $end) {
        try {
            $line = $sp.ReadLine()
            if ($line -match 'L=(-?\d+).*R=(-?\d+)') {
                $L = [int]$Matches[1]; $R = [int]$Matches[2]
            }
        } catch {}   # timeout -> bo qua
    }
    Send-Cmd "x"          # dung
    Start-Sleep -Milliseconds 300
    Send-Cmd "p"          # in gia tri cuoi
    Start-Sleep -Milliseconds 200
    try { while ($true) { $line = $sp.ReadLine(); if ($line -match 'L=(-?\d+).*R=(-?\d+)') { $L=[int]$Matches[1]; $R=[int]$Matches[2] } } } catch {}
    return @{ L = $L; R = $R }
}

$THRESH = 50   # nguong toi thieu de coi la co quay

# ---------- Set toc do ----------
Send-Cmd "v$Speed"
Start-Sleep -Milliseconds 200

# ============================================================
#  TEST BANH TRAI (A)
# ============================================================
Write-Host "[1/2] Test banh TRAI (lenh tien)..." -ForegroundColor Cyan
$a = Read-Encoder "1" $RunMs
Write-Host "      Encoder sau khi chay banh trai: L=$($a.L)  R=$($a.R)"
if ([math]::Abs($a.L) -lt $THRESH) {
    Write-Host "      !! Banh TRAI gan nhu khong quay (L=$($a.L)). Kiem tra phan cung." -ForegroundColor Yellow
}
$needInvertA = ($a.L -lt 0)   # tien ma dem AM -> can dao
if ($needInvertA) {
    Write-Host "      -> Banh TRAI chay NGUOC, dao chieu (iA)" -ForegroundColor Yellow
    Send-Cmd "iA"
    $a2 = Read-Encoder "1" $RunMs
    Write-Host "      Sau khi dao: L=$($a2.L)  R=$($a2.R)"
} else {
    Write-Host "      -> Banh TRAI chieu OK" -ForegroundColor Green
}

# ============================================================
#  TEST BANH PHAI (B)
# ============================================================
Write-Host "`n[2/2] Test banh PHAI (lenh tien)..." -ForegroundColor Cyan
$b = Read-Encoder "2" $RunMs
Write-Host "      Encoder sau khi chay banh phai: L=$($b.L)  R=$($b.R)"
if ([math]::Abs($b.R) -lt $THRESH) {
    Write-Host "      !! Banh PHAI gan nhu khong quay (R=$($b.R)). Kiem tra phan cung." -ForegroundColor Yellow
}
$needInvertB = ($b.R -lt 0)
if ($needInvertB) {
    Write-Host "      -> Banh PHAI chay NGUOC, dao chieu (iB)" -ForegroundColor Yellow
    Send-Cmd "iB"
    $b2 = Read-Encoder "2" $RunMs
    Write-Host "      Sau khi dao: L=$($b2.L)  R=$($b2.R)"
} else {
    Write-Host "      -> Banh PHAI chieu OK" -ForegroundColor Green
}

# Trang thai INVERT cuoi cung (mac dinh code: A=true, B=false)
$finalA = $true; if ($needInvertA) { $finalA = -not $finalA }
$finalB = $false; if ($needInvertB) { $finalB = -not $finalB }

Send-Cmd "x"
$sp.Close()

Write-Host "`n==================== KET QUA ====================" -ForegroundColor Green
Write-Host ("  INVERT_A = {0}" -f $finalA.ToString().ToLower())
Write-Host ("  INVERT_B = {0}" -f $finalB.ToString().ToLower())
Write-Host "=================================================`n"

# ---------- Ghi vao main.cpp ----------
if (-not $NoWrite) {
    $f = Join-Path $PSScriptRoot "src\main.cpp"
    $txt = Get-Content $f -Raw
    $txt = $txt -replace 'bool INVERT_A = (true|false);', ("bool INVERT_A = {0};" -f $finalA.ToString().ToLower())
    $txt = $txt -replace 'bool INVERT_B = (true|false);', ("bool INVERT_B = {0};" -f $finalB.ToString().ToLower())
    Set-Content $f $txt -Encoding utf8
    Write-Host "Da ghi INVERT_A/INVERT_B vao main.cpp. Chay .\flash.ps1 de nap ban chinh thuc." -ForegroundColor Green
} else {
    Write-Host "(-NoWrite) Khong ghi file. Tu set INVERT_A/INVERT_B trong main.cpp neu muon." -ForegroundColor DarkGray
}
