#Requires -Version 5.1
<#
.SYNOPSIS
    PiColor Pil Omru Test Araci

.DESCRIPTION
    WiFi STA uzerinden PiColor cihazina belirtilen araliklarla TCP baglanir,
    olcum / durum sorgusu yapar ve sonuclari CSV dosyasina kaydeder.
    Cihaz yanit vermez hale gelince toplam pil omrunu hesaplayip raporlar.

.PARAMETER HostName
    Cihaz adresi veya IP. Varsayilan: picolor.local

.PARAMETER Port
    TCP port. Varsayilan: 8266

.PARAMETER Interval
    Sorgular arasi bekleme suresi (saniye). Varsayilan: 30

.PARAMETER LogDir
    CSV log dosyasinin kaydedilecegi klasor. Varsayilan: scriptin klasoru

.PARAMETER Command
    Gonderilecek komut. Varsayilan: OKU_0
    Secenekler: OKU_0 (anlik okuma) | DURUM (sistem durumu) | TUM (tam paket)

.PARAMETER Timeout
    TCP baglanti ve yanit zaman asimi (saniye). Varsayilan: 8

.PARAMETER MaxConsecutiveFail
    Arka arkaya bu kadar basarisiz baglanti -> "pil bitti" uyarisi.
    Varsayilan: 5

.PARAMETER StatusInterval
    Her N. sorguda bir ekstra DURUM komutu gonderilir. 0=kapali. Varsayilan: 10

.EXAMPLE
    .\picolor_pil_test.ps1

.EXAMPLE
    .\picolor_pil_test.ps1 -Interval 60 -Command TUM

.EXAMPLE
    .\picolor_pil_test.ps1 -HostName 192.168.1.50 -Interval 10 -MaxConsecutiveFail 3

.EXAMPLE
    .\picolor_pil_test.ps1 -Interval 120 -LogDir "C:\PilTestleri"
#>

param(
    [string] $HostName            = "picolor.local",
    [int]    $Port                = 8266,
    [int]    $Interval            = 30,
    [string] $LogDir              = $PSScriptRoot,
    [string] $Command             = "OKU_0",
    [int]    $Timeout             = 8,
    [int]    $MaxConsecutiveFail  = 5,
    [int]    $StatusInterval      = 10
)

Set-StrictMode -Off
$ErrorActionPreference = "SilentlyContinue"

# -----------------------------------------------------------------------------
#  BASLIK
# -----------------------------------------------------------------------------
Clear-Host
Write-Host ""
Write-Host "  +----------------------------------------------------------+" -ForegroundColor Cyan
Write-Host "  |      PiColor -- Pil Omru Test Araci  v1.0               |" -ForegroundColor Cyan
Write-Host "  +----------------------------------------------------------+" -ForegroundColor Cyan
Write-Host ""

# -----------------------------------------------------------------------------
#  HAZIRLIK
# -----------------------------------------------------------------------------
$startTime       = Get-Date
$logFileName     = "picolor_pil_{0}.csv" -f $startTime.ToString("yyyyMMdd_HHmmss")
$logPath         = Join-Path $LogDir $logFileName
$iteration       = 0
$successCount    = 0
$failCount       = 0
$consecutiveFail = 0
$lastSuccessTime = $null
$batteryDeadTime = $null
$lastWifi        = "?"

if (-not (Test-Path $LogDir)) { New-Item -ItemType Directory -Path $LogDir | Out-Null }

Write-Host ("  {0,-20} {1}" -f "Hedef:",           "$HostName`:$Port")            -ForegroundColor White
Write-Host ("  {0,-20} {1}" -f "Aralik:",           "$Interval saniye")            -ForegroundColor White
Write-Host ("  {0,-20} {1}" -f "Komut:",            $Command)                      -ForegroundColor White
Write-Host ("  {0,-20} {1}" -f "Zaman asimi:",      "$Timeout sn")                 -ForegroundColor White
Write-Host ("  {0,-20} {1}" -f "Pil bitti esigi:",  "$MaxConsecutiveFail ard. hata") -ForegroundColor White
Write-Host ("  {0,-20} {1}" -f "Log:",              $logPath)                      -ForegroundColor Yellow
Write-Host ("  {0,-20} {1}" -f "Baslangic:",        $startTime.ToString("dd.MM.yyyy HH:mm:ss")) -ForegroundColor White
Write-Host ""
Write-Host "  Durdurmak icin: Ctrl+C" -ForegroundColor DarkGray
Write-Host ""
Write-Host ("  {0,-10} {1,-10} {2,-8} {3,-8} {4,8} {5,8} {6,8}  {7}" -f `
    "Saat", "Sure", "Sorgu#", "Durum", "R", "G", "B", "WiFi") -ForegroundColor DarkGray
Write-Host ("  " + ("-" * 70)) -ForegroundColor DarkGray

# -----------------------------------------------------------------------------
#  CSV BASLIGI
# -----------------------------------------------------------------------------
"zaman,sure_sn,sorgu_no,durum,yanit_ms,R,G,B,wifi,meta,ham_yanit" |
    Out-File -FilePath $logPath -Encoding ASCII

# -----------------------------------------------------------------------------
#  YARDIMCI: Sure bicimlendirme  HH:MM:SS
# -----------------------------------------------------------------------------
function Format-Duration ([int]$totalSec) {
    "{0:D2}:{1:D2}:{2:D2}" -f ([int]($totalSec / 3600)), ([int](($totalSec % 3600) / 60)), ($totalSec % 60)
}

# -----------------------------------------------------------------------------
#  YARDIMCI: PiColor cikti satirini ayristir
#  Format: ts;type;mode;v1;v2;v3[;meta];wifi=X;user=Y
# -----------------------------------------------------------------------------
function Parse-Line ([string]$line) {
    $p = $line.Split(';')
    $r = [PSCustomObject]@{
        Valid = $false; Type = ""; Mode = ""
        V1 = ""; V2 = ""; V3 = ""; Meta = ""; Wifi = ""; Raw = $line
    }
    if ($p.Count -lt 7) { return $r }

    $r.Type = $p[1]
    $r.Mode = if ($p[2] -eq "0") { "STABIL" } else { "DINAMIK" }
    $r.V1   = $p[3]
    $r.V2   = $p[4]
    $r.V3   = $p[5]

    for ($i = 6; $i -lt $p.Count; $i++) {
        if     ($p[$i] -like "wifi=*") { $r.Wifi = $p[$i].Substring(5) }
        elseif ($p[$i] -like "user=*") { }
        elseif ($p[$i].Length -gt 0)   { $r.Meta = $p[$i] }
    }

    $r.Valid = $true
    return $r
}

# -----------------------------------------------------------------------------
#  YARDIMCI: TCP sorgusu yap
# -----------------------------------------------------------------------------
function Invoke-PiColorQuery ([string]$cmd) {
    $res = [PSCustomObject]@{ OK = $false; Ms = 0; Line = ""; ErrorMsg = "" }
    $sw  = [Diagnostics.Stopwatch]::StartNew()
    $client = $null

    try {
        $client = New-Object Net.Sockets.TcpClient
        $task   = $client.ConnectAsync($HostName, $Port)

        if (-not $task.Wait($Timeout * 1000)) {
            $res.ErrorMsg = "TIMEOUT (baglanti $Timeout sn)"
            return $res
        }
        if (-not $client.Connected) {
            $res.ErrorMsg = "BAGLANTI REDDEDILDI"
            return $res
        }

        $stream = $client.GetStream()
        $stream.ReadTimeout  = $Timeout * 1000
        $stream.WriteTimeout = $Timeout * 1000

        $bytes = [Text.Encoding]::UTF8.GetBytes($cmd + "`n")
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()

        $reader   = New-Object IO.StreamReader($stream, [Text.Encoding]::UTF8)
        $deadline = [DateTime]::Now.AddSeconds($Timeout)

        while ([DateTime]::Now -lt $deadline) {
            if ($client.Available -gt 0 -or $stream.DataAvailable) {
                $line = $reader.ReadLine()
                if ($line -and $line.Contains(";")) {
                    $res.Line = $line.Trim()
                    $res.OK   = $true
                    $res.Ms   = [int]$sw.ElapsedMilliseconds
                    break
                }
            } else {
                Start-Sleep -Milliseconds 80
            }
        }

        if (-not $res.OK) { $res.ErrorMsg = "TIMEOUT (yanit $Timeout sn)" }

    } catch {
        $res.ErrorMsg = $_.Exception.Message -replace "`r`n", " "
    } finally {
        try { if ($client) { $client.Close() } } catch {}
        $sw.Stop()
    }

    return $res
}

# -----------------------------------------------------------------------------
#  OZET RAPOR  (Ctrl+C veya pil bitti)
# -----------------------------------------------------------------------------
function Show-Summary ([string]$reason) {
    $endTime  = Get-Date
    $totalSec = [int]($endTime - $startTime).TotalSeconds

    $pilOmruSn = if ($lastSuccessTime) {
        [int]($lastSuccessTime - $startTime).TotalSeconds
    } else { 0 }

    Write-Host ""
    Write-Host ""
    Write-Host ("  " + ("=" * 65)) -ForegroundColor DarkCyan
    Write-Host "  TEST OZETI -- $reason" -ForegroundColor Cyan
    Write-Host ("  " + ("=" * 65)) -ForegroundColor DarkCyan
    Write-Host ""
    Write-Host ("  {0,-24} {1}" -f "Baslangic:",     $startTime.ToString("dd.MM.yyyy HH:mm:ss")) -ForegroundColor White
    Write-Host ("  {0,-24} {1}" -f "Bitis:",          $endTime.ToString("dd.MM.yyyy HH:mm:ss"))   -ForegroundColor White
    Write-Host ("  {0,-24} {1}" -f "Test suresi:",    ((Format-Duration $totalSec) + " ($totalSec sn)")) -ForegroundColor White
    Write-Host ""

    if ($pilOmruSn -gt 0) {
        $pilStr = (Format-Duration $pilOmruSn) + " ($pilOmruSn sn)"
        $col    = if ($pilOmruSn -gt 7200) { "Green" } elseif ($pilOmruSn -gt 3600) { "Yellow" } else { "Red" }
        Write-Host ("  {0,-24} {1}" -f ">>> PIL OMRU <<<",    $pilStr)                                          -ForegroundColor $col
        Write-Host ("  {0,-24} {1}" -f "Son basarili yanit:", $lastSuccessTime.ToString("HH:mm:ss"))            -ForegroundColor Yellow
    } else {
        Write-Host "  [!] Cihaza hic ulasilamadi -- IP/port/WiFi kontrol edin." -ForegroundColor Red
    }

    Write-Host ""
    Write-Host ("  {0,-24} {1}" -f "Toplam sorgu:",  $iteration)     -ForegroundColor White
    Write-Host ("  {0,-24} {1}" -f "Basarili:",      $successCount)  -ForegroundColor Green
    Write-Host ("  {0,-24} {1}" -f "Basarisiz:",     $failCount)     -ForegroundColor $(if ($failCount -gt 0) { "Red" } else { "Gray" })

    $oran = if ($iteration -gt 0) { [int](($successCount / $iteration) * 100) } else { 0 }
    Write-Host ("  {0,-24} {1}%" -f "Basari orani:", $oran) -ForegroundColor $(if ($oran -ge 90) { "Green" } else { "Yellow" })

    Write-Host ""
    Write-Host ("  {0,-24} {1}" -f "Log dosyasi:", $logPath) -ForegroundColor Cyan
    Write-Host ("  " + ("=" * 65)) -ForegroundColor DarkCyan
    Write-Host ""

    $ozet = @"

# === TEST OZETI ============================================================
# Sebep           : $reason
# Baslangic       : $($startTime.ToString("dd.MM.yyyy HH:mm:ss"))
# Bitis           : $($endTime.ToString("dd.MM.yyyy HH:mm:ss"))
# Pil omru (sn)   : $pilOmruSn
# Pil omru (saat) : $(Format-Duration $pilOmruSn)
# Toplam sorgu    : $iteration
# Basarili        : $successCount
# Basarisiz       : $failCount
"@
    $ozet | Add-Content -Path $logPath -Encoding ASCII
}

# -----------------------------------------------------------------------------
#  ANA DONGU
# -----------------------------------------------------------------------------
try {
    while ($true) {
        $iteration++
        $elapsed = [int]((Get-Date) - $startTime).TotalSeconds
        $durStr  = Format-Duration $elapsed
        $saat    = (Get-Date).ToString("HH:mm:ss")

        # Her StatusInterval sorguda bir DURUM da gonder
        $sorguKomutu = $Command
        if ($StatusInterval -gt 0 -and ($iteration % $StatusInterval) -eq 0) {
            $sorguKomutu = "DURUM"
        }

        $q = Invoke-PiColorQuery $sorguKomutu

        $sorguStr = "#{0:D4}" -f $iteration

        if ($q.OK) {
            $p = Parse-Line $q.Line
            $successCount++
            $consecutiveFail = 0
            $lastSuccessTime = Get-Date
            if ($p.Wifi) { $lastWifi = $p.Wifi }

            $wifiRenk = if ($lastWifi -eq "AP") { "DarkYellow" } `
                        elseif ($lastWifi -eq "?") { "DarkGray" } `
                        else { "Cyan" }

            Write-Host ("  {0}  {1}  {2}  " -f $saat, $durStr, $sorguStr) -NoNewline -ForegroundColor DarkGray
            Write-Host ("[OK] {0,4}ms  " -f $q.Ms) -NoNewline -ForegroundColor Green

            if ($p.Valid -and $p.Type -ne "4") {
                Write-Host "R:" -NoNewline -ForegroundColor Red
                Write-Host ("{0,7}  " -f $p.V1) -NoNewline -ForegroundColor White
                Write-Host "G:" -NoNewline -ForegroundColor Green
                Write-Host ("{0,7}  " -f $p.V2) -NoNewline -ForegroundColor White
                Write-Host "B:" -NoNewline -ForegroundColor Blue
                Write-Host ("{0,7}  " -f $p.V3) -NoNewline -ForegroundColor White
            } else {
                $metaKisa = if ($p.Meta.Length -gt 28) { $p.Meta.Substring(0,28) } else { $p.Meta }
                Write-Host ("{0,-30}" -f $metaKisa) -NoNewline -ForegroundColor DarkGray
            }

            Write-Host $lastWifi -ForegroundColor $wifiRenk

            $csvSatir = "{0},{1},{2},OK,{3},{4},{5},{6},{7},{8},{9}" -f `
                $saat, $elapsed, $iteration, $q.Ms,
                $p.V1, $p.V2, $p.V3, $lastWifi, $p.Meta,
                ('"' + $q.Line.Replace('"', "'") + '"')
            $csvSatir | Add-Content -Path $logPath -Encoding ASCII

        } else {
            $failCount++
            $consecutiveFail++

            Write-Host ("  {0}  {1}  {2}  " -f $saat, $durStr, $sorguStr) -NoNewline -ForegroundColor DarkGray
            Write-Host "[ER] " -NoNewline -ForegroundColor Red
            Write-Host $q.ErrorMsg  -ForegroundColor DarkRed

            $csvSatir = "{0},{1},{2},HATA,0,,,,{3},{4},{5}" -f `
                $saat, $elapsed, $iteration, $lastWifi, $q.ErrorMsg, '""'
            $csvSatir | Add-Content -Path $logPath -Encoding ASCII

            if ($consecutiveFail -ge $MaxConsecutiveFail) {
                Write-Host ""
                Write-Host ("  [!] {0} ard. hata -- cihaz yanit vermiyor." -f $consecutiveFail) -ForegroundColor Red
                if ($lastSuccessTime) {
                    $pilSn  = [int]($lastSuccessTime - $startTime).TotalSeconds
                    Write-Host ("  [!] Pil omru tahmini: {0}  ({1} sn)" -f (Format-Duration $pilSn), $pilSn) -ForegroundColor Yellow
                }
                $batteryDeadTime = Get-Date
                break
            }
        }

        # Geri sayim gostergesi
        for ($s = $Interval; $s -gt 0; $s--) {
            Write-Host -NoNewline ("`r  Sonraki sorgu: {0,3} sn ...   " -f $s) -ForegroundColor DarkGray
            Start-Sleep -Seconds 1
        }
        Write-Host -NoNewline ("`r" + (" " * 38) + "`r")

    }
} finally {
    $sebep = if ($batteryDeadTime) { "CIHAZ YANIT VERMIYOR (pil bitti?)" } else { "Kullanici durdurdu (Ctrl+C)" }
    Show-Summary $sebep
}
