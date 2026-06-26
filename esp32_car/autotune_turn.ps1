# ============================================================
#  TU HIEU CHUAN PID RE (turn) qua Serial
#  - Thu nhieu cap (Kp, Kd), moi cap re +90 va -90
#  - Doc dong "[turnres] ... err=.. over=.. ms=.. settled=.."
#  - Cham diem, chon cap tot nhat, lap den khi DAT tieu chi
#  - Ghi KpT/KdT tot nhat vao src/main.cpp
#
#  Cach dung:
#     .\autotune_turn.ps1
#     .\autotune_turn.ps1 -Port COM5
#     .\autotune_turn.ps1 -NoWrite        # khong ghi file
#
#  LUU Y: dat xe tren san co the quay tu do (banh cham dat), du cho ~1m2,
#         dong Serial Monitor truoc khi chay.
# ============================================================
param(
    [string]$Port = "",
    [switch]$NoWrite,
    # Tieu chi DAT (trung binh 2 lan re):
    [double]$MaxErrDeg  = 2.0,    # sai so goc
    [double]$MaxOverDeg = 3.0,    # vot lo
    [int]   $MaxMs      = 1800    # thoi gian on dinh
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

# ---------- Tim cong (uu tien USB-UART that, bo qua Bluetooth) ----------
if ($Port -eq "") {
    $dev = Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
           Where-Object { $_.Name -match '\(COM\d+\)' -and $_.Name -match 'CP210|CH340|CH910|UART|USB|Silicon' } |
           Select-Object -First 1
    if ($dev -and $dev.Name -match '\((COM\d+)\)') {
        $Port = $Matches[1]
        Write-Host "Auto-detect ESP32: $Port  ($($dev.Name))" -ForegroundColor DarkGray
    } else {
        $ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
        if ($ports.Count -eq 0) { Write-Host "Khong thay cong COM." -ForegroundColor Red; exit 1 }
        $Port = $ports[-1]
        Write-Host "Khong nhan dien duoc USB-UART, dung $Port" -ForegroundColor Yellow
    }
}
Get-Process pio,python -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 400

# ---------- Mo cong ----------
$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, None, 8, one
$sp.DtrEnable = $false; $sp.RtsEnable = $false
$sp.ReadTimeout = 300; $sp.NewLine = "`n"
$sp.Open(); Start-Sleep -Milliseconds 500; $sp.DiscardInBuffer()
Write-Host "Da mo $Port @115200`n" -ForegroundColor Green

function Send-Cmd([string]$c) { $sp.WriteLine($c); Start-Sleep -Milliseconds 120 }

# Chay 1 lan re $deg do, doc dong [turnres], tra ve hashtable metrics (hoac $null)
function Run-Turn([double]$deg) {
    $sp.DiscardInBuffer()
    Send-Cmd "e"                       # reset odometry
    Start-Sleep -Milliseconds 200
    $sp.DiscardInBuffer()
    Send-Cmd "T$deg"                   # re
    $deadline = (Get-Date).AddSeconds(6)
    while ((Get-Date) -lt $deadline) {
        try {
            $line = $sp.ReadLine()
            if ($line -match '\[turnres\].*err=(-?\d+\.?\d*).*over=(-?\d+\.?\d*).*ms=(\d+).*settled=(\d)') {
                return @{
                    err     = [math]::Abs([double]$Matches[1])
                    over    = [double]$Matches[2]
                    ms      = [int]$Matches[3]
                    settled = [int]$Matches[4]
                }
            }
        } catch {}
    }
    return $null
}

# Danh gia 1 cap (Kp,Kd): re +90 va -90, lay trung binh
function Eval-Gains([double]$kp, [double]$kd) {
    Send-Cmd "Tp$kp"; Send-Cmd "Td$kd"
    $r1 = Run-Turn 90
    Start-Sleep -Milliseconds 300
    $r2 = Run-Turn -90
    if ($null -eq $r1 -or $null -eq $r2) { return $null }
    $err  = ($r1.err  + $r2.err)  / 2
    $over = ($r1.over + $r2.over) / 2
    $ms   = [int]((($r1.ms + $r2.ms) / 2))
    $settled = [math]::Min($r1.settled, $r2.settled)
    # diem: thap = tot. Phat nang neu khong on dinh.
    $score = (1 - $settled) * 1000 + $over * 10 + $ms * 0.05 + $err * 5
    return @{ kp=$kp; kd=$kd; err=$err; over=$over; ms=$ms; settled=$settled; score=$score }
}

function Meets([hashtable]$m) {
    return ($m.settled -eq 1) -and ($m.err -le $MaxErrDeg) -and `
           ($m.over -le $MaxOverDeg) -and ($m.ms -le $MaxMs)
}

# ---------- Luoi tim kiem ----------
$kpList = @(80, 110, 140, 170, 200)
$kdList = @(20, 40, 60, 80)

$best = $null
$achieved = $null
Write-Host "Bat dau quet luoi PID re ($($kpList.Count)x$($kdList.Count) cap)...`n" -ForegroundColor Cyan

:outer foreach ($kp in $kpList) {
    foreach ($kd in $kdList) {
        Write-Host ("  Thu Kp={0,-4} Kd={1,-4} ... " -f $kp, $kd) -NoNewline
        $m = Eval-Gains $kp $kd
        if ($null -eq $m) { Write-Host "khong doc duoc ket qua" -ForegroundColor DarkYellow; continue }
        Write-Host ("err={0:F2} over={1:F2} ms={2} settled={3} score={4:F1}" -f $m.err,$m.over,$m.ms,$m.settled,$m.score)
        if ($null -eq $best -or $m.score -lt $best.score) { $best = $m }
        if (Meets $m) { $achieved = $m; Write-Host "  -> DAT tieu chi!" -ForegroundColor Green; break outer }
    }
}

# ---------- Tinh chinh quanh diem tot nhat (neu chua dat) ----------
if ($null -eq $achieved -and $null -ne $best) {
    Write-Host "`nTinh chinh quanh Kp=$($best.kp) Kd=$($best.kd)..." -ForegroundColor Cyan
    foreach ($dkp in @(-20, 0, 20)) {
        foreach ($dkd in @(-10, 0, 10)) {
            $kp = $best.kp + $dkp; $kd = $best.kd + $dkd
            if ($kp -le 0 -or $kd -lt 0) { continue }
            if ($dkp -eq 0 -and $dkd -eq 0) { continue }
            Write-Host ("  Thu Kp={0,-4} Kd={1,-4} ... " -f $kp, $kd) -NoNewline
            $m = Eval-Gains $kp $kd
            if ($null -eq $m) { Write-Host "loi" -ForegroundColor DarkYellow; continue }
            Write-Host ("err={0:F2} over={1:F2} ms={2} settled={3} score={4:F1}" -f $m.err,$m.over,$m.ms,$m.settled,$m.score)
            if ($m.score -lt $best.score) { $best = $m }
            if (Meets $m) { $achieved = $m; Write-Host "  -> DAT tieu chi!" -ForegroundColor Green; break }
        }
        if ($achieved) { break }
    }
}

Send-Cmd "x"
$sp.Close()

$final = if ($achieved) { $achieved } else { $best }
if ($null -eq $final) { Write-Host "`nKhong thu duoc ket qua nao. Kiem tra ket noi/banh xe." -ForegroundColor Red; exit 1 }

Write-Host "`n==================== KET QUA ====================" -ForegroundColor Green
if ($achieved) { Write-Host "  Trang thai: DAT tieu chi" -ForegroundColor Green }
else { Write-Host "  Trang thai: CHUA dat het, dung cap tot nhat" -ForegroundColor Yellow }
Write-Host ("  KpT = {0}" -f $final.kp)
Write-Host ("  KdT = {0}" -f $final.kd)
Write-Host ("  err={0:F2}deg  over={1:F2}deg  ms={2}  settled={3}" -f $final.err,$final.over,$final.ms,$final.settled)
Write-Host "=================================================`n"

# ---------- Ghi vao main.cpp ----------
if (-not $NoWrite) {
    $f = Join-Path $PSScriptRoot "src\main.cpp"
    $txt = Get-Content $f -Raw
    $txt = $txt -replace 'float KpT = [\d.]+, KdT = [\d.]+;', ("float KpT = {0}.0, KdT = {1}.0;" -f [int]$final.kp, [int]$final.kd)
    Set-Content $f $txt -Encoding utf8
    Write-Host "Da ghi KpT/KdT vao main.cpp. Chay .\flash.ps1 de nap ban chinh thuc." -ForegroundColor Green
} else {
    Write-Host "(-NoWrite) Khong ghi file." -ForegroundColor DarkGray
}
