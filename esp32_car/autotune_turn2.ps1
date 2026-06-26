# ============================================================
#  AUTOTUNE PID RE v2  -  Nhan dang he + tinh THANG Kp/Kd
#
#  Khac ban cu (quet luoi 20 lan, motor nong):
#   - Chi chay 1-2 lan lay STEP RESPONSE day du (trace).
#   - Phan tich: do vot lo (Mp) + thoi gian dinh (tp)
#       -> he so giam chan zeta, tan so rieng wn
#       -> nhan dang plant (b, c)
#       -> DAT CUC theo zeta* mong muon, tinh Kp*/Kd*.
#   - Co NGHI lam mat L298N giua cac lan chay.
#   - Verify 1 lan voi gain moi (runtime, khong can flash).
#
#  Cach dung:
#     .\autotune_turn2.ps1 -Port COM5
#     .\autotune_turn2.ps1 -Port COM5 -ZetaTarget 0.7 -SpeedFactor 1.0
#     .\autotune_turn2.ps1 -Port COM5 -NoWrite          # khong ghi main.cpp
#
#  -ZetaTarget : 0.7 ~ vot lo ~5% (muot). 1.0 = khong vot lo (cham hon).
#  -SpeedFactor: 1.0 giu nguyen toc do (chi sua giam chan). >1 nhanh hon
#                nhung motor chay manh hon. <1 nhe nhang hon.
#
#  LUU Y: dat xe cho banh quay tu do, dong Serial Monitor truoc khi chay.
# ============================================================
param(
    [string]$Port = "",
    [double]$ZetaTarget  = 0.7,
    [double]$SpeedFactor = 1.0,
    [double]$TestDeg     = 90,
    [int]   $CooldownMs  = 3000,    # nghi lam mat motor giua cac lan chay
    [switch]$NoWrite
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

# ---------- Tim cong ----------
if ($Port -eq "") {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
    if ($ports.Count -eq 0) { Write-Host "Khong thay cong COM." -ForegroundColor Red; exit 1 }
    $Port = $ports[-1]
}
Get-Process pio,python -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 400

# ---------- Mo cong ----------
$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, None, 8, one
$sp.DtrEnable = $false; $sp.RtsEnable = $false
$sp.ReadTimeout = 400; $sp.NewLine = "`n"
$sp.Open(); Start-Sleep -Milliseconds 500; $sp.DiscardInBuffer()
Write-Host "Da mo $Port @115200`n" -ForegroundColor Green

function Send-Cmd([string]$c) { $sp.WriteLine($c); Start-Sleep -Milliseconds 120 }

# Dam bao turnVerbose = trang thai mong muon (TV la lenh toggle)
function Set-Verbose([int]$want) {
    for ($try = 0; $try -lt 4; $try++) {
        $sp.DiscardInBuffer()
        $sp.WriteLine("TV"); Start-Sleep -Milliseconds 150
        $deadline = (Get-Date).AddSeconds(1.5)
        while ((Get-Date) -lt $deadline) {
            try { $line = $sp.ReadLine() } catch { break }
            if ($line -match 'turnVerbose=(\d)') {
                if ([int]$Matches[1] -eq $want) { return $true }
                break   # sai trang thai -> toggle lai
            }
        }
    }
    return $false
}

# Chay 1 lan re, thu thap trace [trace_begin..trace_end] + dong [turnres].
# Tra ve PSObject: tgt, kp, kd, t[], a[], over, err, settled
function Run-Trace([double]$deg) {
    $sp.DiscardInBuffer()
    Send-Cmd "e"; Start-Sleep -Milliseconds 200; $sp.DiscardInBuffer()
    Send-Cmd "T$deg"

    $T = New-Object System.Collections.Generic.List[double]
    $A = New-Object System.Collections.Generic.List[double]
    $res = $null; $hdr = $null; $inTrace = $false
    $deadline = (Get-Date).AddSeconds(8)
    while ((Get-Date) -lt $deadline) {
        try { $line = $sp.ReadLine() } catch { continue }
        if ($line -match '^\[turnres\].*over=(-?\d+\.?\d*).*ms=(\d+).*settled=(\d)') {
            $res = @{ over=[double]$Matches[1]; ms=[int]$Matches[2]; settled=[int]$Matches[3] }
        }
        elseif ($line -match '^\[trace_begin\] tgt=(-?\d+\.?\d*) kp=(-?\d+\.?\d*) kd=(-?\d+\.?\d*)') {
            $hdr = @{ tgt=[double]$Matches[1]; kp=[double]$Matches[2]; kd=[double]$Matches[3] }
            $inTrace = $true
        }
        elseif ($line -match '^\[trace_end\]') { break }
        elseif ($inTrace -and $line -match '^(\d+),(-?\d+\.?\d*)') {
            $T.Add([double]$Matches[1] / 1000.0)   # ms -> s
            $A.Add([double]$Matches[2])
        }
    }
    if ($null -eq $hdr -or $T.Count -lt 5) { return $null }
    return [PSCustomObject]@{
        tgt = $hdr.tgt; kp = $hdr.kp; kd = $hdr.kd
        t = $T.ToArray(); a = $A.ToArray()
        over = if ($res) { $res.over } else { 0 }
        settled = if ($res) { $res.settled } else { 0 }
    }
}

# Phan tich step-response -> Mp (vot lo), tp (thoi gian dinh), zeta, wn
function Analyze-Step($tr) {
    $t = $tr.t; $a = $tr.a; $tgt = $tr.tgt
    $n = $a.Count
    # gia tri xac lap = trung binh 20% cuoi
    $k = [Math]::Max(1, [int]($n * 0.2))
    $final = 0.0; for ($i = $n - $k; $i -lt $n; $i++) { $final += $a[$i] }; $final /= $k
    if ($final -le 1) { $final = $tgt }
    # dinh dau tien
    $amax = $a[0]; $tp = $t[0]; $imax = 0
    for ($i = 1; $i -lt $n; $i++) { if ($a[$i] -gt $amax) { $amax = $a[$i]; $tp = $t[$i]; $imax = $i } }
    $Mp = ($amax - $final) / $final          # vot lo (phan so so voi xac lap)
    $sseDeg = $tgt - $final                   # sai so xac lap (deg)

    $obj = [PSCustomObject]@{
        final=$final; amax=$amax; tp=$tp; Mp=$Mp; sseDeg=$sseDeg
        zeta=$null; wn=$null; mode=""
    }
    if ($Mp -gt 0.05 -and $tp -gt 0.02 -and $imax -gt 0) {
        # he thieu giam chan ro ret -> tinh duoc zeta, wn (dat cuc)
        $lnMp = [Math]::Log($Mp)
        $zeta = -$lnMp / [Math]::Sqrt([Math]::PI*[Math]::PI + $lnMp*$lnMp)
        $wd = [Math]::PI / $tp                 # tp = pi/wd
        $wn = $wd / [Math]::Sqrt(1 - $zeta*$zeta)
        $obj.zeta = $zeta; $obj.wn = $wn; $obj.mode = "underdamped"
    }
    elseif ($sseDeg -gt 3)  { $obj.mode = "undershoot" }   # final < tgt: chua toi -> tang Kp
    elseif ($sseDeg -lt -3) { $obj.mode = "overshoot"  }   # final > tgt: qua da -> giam Kp/tang Kd
    else                    { $obj.mode = "good" }          # |sse|<=3: dat
    return $obj
}

Write-Host "B1) Bat trace, chay step response tai gain hien tai..." -ForegroundColor Cyan
if (-not (Set-Verbose 1)) { Write-Host "Khong bat duoc trace (TV). Kiem tra firmware." -ForegroundColor Red; $sp.Close(); exit 1 }
$tr1 = Run-Trace $TestDeg
if ($null -eq $tr1) { Write-Host "Khong doc duoc trace. Kiem tra firmware da nap ban moi chua." -ForegroundColor Red; $sp.Close(); exit 1 }

$an1 = Analyze-Step $tr1
$csv1 = Join-Path $PSScriptRoot "turn_trace_1.csv"
$lines1 = @("t_s,ang_deg") + (0..($tr1.t.Count-1) | ForEach-Object { "{0:F3},{1:F2}" -f $tr1.t[$_],$tr1.a[$_] })
$lines1 -join "`r`n" | Set-Content $csv1 -Encoding utf8

Write-Host ("   gain test: Kp={0} Kd={1}" -f $tr1.kp,$tr1.kd)
Write-Host ("   xac lap={0:F1}deg  dinh={1:F1}deg  Mp={2:P1}  tp={3:F2}s  sse={4:F1}deg  mode={5}" `
            -f $an1.final,$an1.amax,$an1.Mp,$an1.tp,$an1.sseDeg,$an1.mode)
if ($an1.zeta) { Write-Host ("   -> zeta={0:F2}  wn={1:F2} rad/s" -f $an1.zeta,$an1.wn) }
Write-Host "   (trace luu: $csv1)`n"

# ---------- Tinh Kp/Kd moi ----------
$kp0 = $tr1.kp; $kd0 = $tr1.kd
$newKp = $kp0; $newKd = $kd0
if ($an1.zeta -ne $null) {
    # Nhan dang plant: wn^2 = b*Kp ;  2*zeta*wn = c + b*Kd
    $b = ($an1.wn * $an1.wn) / $kp0
    $c = 2*$an1.zeta*$an1.wn - $kd0*$b
    $wnT = $an1.wn * $SpeedFactor          # tan so rieng muc tieu
    $newKp = ($wnT*$wnT) / $b
    $newKd = (2*$ZetaTarget*$wnT - $c) / $b
    Write-Host ("B2) Underdamped -> dat cuc. Nhan dang b={0:F3} c={1:F3}; zeta*={2} wn*={3:F2}" -f $b,$c,$ZetaTarget,$wnT) -ForegroundColor Cyan
}
elseif ($an1.mode -eq "undershoot") {
    $newKp = $kp0 * 1.2; $newKd = $kd0           # chua toi dich -> tang Kp
    Write-Host "B2) Undershoot (chua toi dich) -> tang Kp 20%" -ForegroundColor Cyan
}
elseif ($an1.mode -eq "overshoot") {
    $newKp = $kp0 * 0.9; $newKd = $kd0 * 1.25     # qua da -> giam Kp, tang Kd
    Write-Host "B2) Overshoot (qua da) -> giam Kp 10%, tang Kd 25%" -ForegroundColor Cyan
}
else {
    Write-Host "B2) Gain hien tai DA TOT (|sse|<=3deg, khong vot lo) -> giu nguyen" -ForegroundColor Green
    $newKp = $kp0; $newKd = $kd0
}
# Gioi han buoc thay doi moi lan (tranh nhay manh gay dao dong)
$newKp = [Math]::Max($kp0*0.7, [Math]::Min($kp0*1.4, $newKp))
$newKd = [Math]::Max($kd0*0.6, [Math]::Min($kd0*1.6, $newKd))
# Gioi han an toan tuyet doi
$newKp = [Math]::Round([Math]::Max(40, [Math]::Min(400, $newKp)))
$newKd = [Math]::Round([Math]::Max(0,  [Math]::Min(200, $newKd)))
Write-Host ("    => Kp*={0}  Kd*={1}`n" -f $newKp,$newKd) -ForegroundColor Green

# ---------- Verify (runtime, khong flash) ----------
Write-Host "Nghi lam mat motor $([int]($CooldownMs/1000))s..." -ForegroundColor DarkGray
Start-Sleep -Milliseconds $CooldownMs

Write-Host "B3) Verify voi gain moi (quay NGUOC lai de tranh xoan cap)..." -ForegroundColor Cyan
Send-Cmd ("Tp{0}" -f $newKp); Send-Cmd ("Td{0}" -f $newKd)
$tr2 = Run-Trace (-$TestDeg)
if ($tr2) {
    $an2 = Analyze-Step $tr2
    $csv2 = Join-Path $PSScriptRoot "turn_trace_2.csv"
    $lines2 = @("t_s,ang_deg") + (0..($tr2.t.Count-1) | ForEach-Object { "{0:F3},{1:F2}" -f $tr2.t[$_],$tr2.a[$_] })
    $lines2 -join "`r`n" | Set-Content $csv2 -Encoding utf8
    Write-Host ("   xac lap={0:F1}deg  Mp={1:P1}  tp={2:F2}s  sse={3:F1}deg  over={4:F2}  settled={5}" `
                -f $an2.final,$an2.Mp,$an2.tp,$an2.sseDeg,$tr2.over,$tr2.settled)
    Write-Host "   (trace luu: $csv2)"
}

Send-Cmd "x"; Set-Verbose 0 | Out-Null     # dung + tat trace
$sp.Close()

# ---------- Ghi main.cpp ----------
Write-Host ""
if (-not $NoWrite) {
    $f = Join-Path $PSScriptRoot "src\main.cpp"
    $txt = Get-Content $f -Raw
    $txt = $txt -replace 'float KpT = [\d.]+, KdT = [\d.]+;', ("float KpT = {0}.0, KdT = {1}.0;" -f [int]$newKp, [int]$newKd)
    Set-Content $f $txt -Encoding utf8
    Write-Host ("Da ghi KpT={0} KdT={1} vao src/main.cpp. Chay .\flash.ps1 de nap chinh thuc." -f [int]$newKp,[int]$newKd) -ForegroundColor Green
} else {
    Write-Host "(-NoWrite) Khong ghi file. Gain tinh duoc: Kp=$newKp Kd=$newKd" -ForegroundColor DarkGray
}
