#Requires -Version 5.1
<#
.SYNOPSIS
    PiColor Pil Ömrü Test Aracı

.DESCRIPTION
    WiFi STA üzerinden PiColor cihazına belirtilen aralıklarla TCP bağlanır,
    ölçüm / durum sorgusu yapar ve sonuçları CSV dosyasına kaydeder.
    Cihaz yanıt vermez hale gelince toplam pil ömrünü hesaplayıp raporlar.

    Çıktı formatı (picolor protokolü):
        timestamp;type;mode;v1;v2;v3[;meta];wifi=<durum>;user=<kullanici>

.PARAMETER HostName
    Cihaz adresi veya IP. Varsayılan: picolor.local

.PARAMETER Port
    TCP port. Varsayılan: 8266

.PARAMETER Interval
    Sorgular arası bekleme süresi (saniye). Varsayılan: 30

.PARAMETER LogDir
    CSV log dosyasının kaydedileceği klasör. Varsayılan: scriptin bulunduğu klasör

.PARAMETER Command
    Her sorguda gönderilecek komut.
    Öneriler: OKU_0 (anlık okuma) | DURUM (sistem durumu) | TUM (tam paket)
    Varsayılan: OKU_0

.PARAMETER Timeout
    TCP bağlantı ve yanıt zaman aşımı (saniye). Varsayılan: 8

.PARAMETER MaxConsecutiveFail
    Arka arkaya bu kadar başarısız bağlantı → "pil bitti" uyarısı verilir.
    Varsayılan: 5

.PARAMETER StatusInterval
    Her N. sorguda bir ekstra DURUM komutu gönderilir (WiFi bilgisi için).
    0 = devre dışı. Varsayılan: 10

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
    [string] $HostName             = "picolor.local",
    [int]    $Port                 = 8266,
    [int]    $Interval             = 30,
    [string] $LogDir               = $PSScriptRoot,
    [string] $Command              = "OKU_0",
    [int]    $Timeout              = 8,
    [int]    $MaxConsecutiveFail   = 5,
    [int]    $StatusInterval       = 10
)

Set-StrictMode -Off
$ErrorActionPreference = "SilentlyContinue"

# ─────────────────────────────────────────────────────────────────────────────
#  BAŞLIK
# ─────────────────────────────────────────────────────────────────────────────
Clear-Host
Write-Host ""
Write-Host "  ╔═══════════════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "  ║      PiColor — Pil Ömrü Test Aracı  v1.0                 ║" -ForegroundColor Cyan
Write-Host "  ╚═══════════════════════════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""

# ─────────────────────────────────────────────────────────────────────────────
#  HAZIRLIK
# ─────────────────────────────────────────────────────────────────────────────
$startTime        = Get-Date
$logFileName      = "picolor_pil_{0}.csv" -f $startTime.ToString("yyyyMMdd_HHmmss")
$logPath          = Join-Path $LogDir $logFileName
$iteration        = 0
$successCount     = 0
$failCount        = 0
$consecutiveFail  = 0
$lastSuccessTime  = $null
$batteryDeadTime  = $null
$lastWifi         = "?"

# Klasör yoksa oluştur
if (-not (Test-Path $LogDir)) { New-Item -ItemType Directory -Path $LogDir | Out-Null }

Write-Host ("  {0,-15} {1}" -f "Hedef:", "$HostName`:$Port")         -ForegroundColor White
Write-Host ("  {0,-15} {1}" -f "Aralık:", "$Interval saniye")        -ForegroundColor White
Write-Host ("  {0,-15} {1}" -f "Komut:", $Command)                   -ForegroundColor White
Write-Host ("  {0,-15} {1}" -f "Zaman aşımı:", "$Timeout sn")        -ForegroundColor White
Write-Host ("  {0,-15} {1}" -f "Pil bitti eşiği:", "$MaxConsecutiveFail arka arkaya hata") -ForegroundColor White
Write-Host ("  {0,-15} {1}" -f "Log:", $logPath)                     -ForegroundColor Yellow
Write-Host ("  {0,-15} {1}" -f "Başlangıç:", $startTime.ToString("dd.MM.yyyy HH:mm:ss")) -ForegroundColor White
Write-Host ""
Write-Host "  Durdurmak için: Ctrl+C" -ForegroundColor DarkGray
Write-Host ""
Write-Host ("  {0,-10} {1,-10} {2,-12} {3,-8} {4,8} {5,8} {6,8}  {7}" -f `
    "Saat", "Süre", "Sorgu#", "Durum", "R", "G", "B", "WiFi") -ForegroundColor DarkGray
Write-Host ("  " + ("─" * 72)) -ForegroundColor DarkGray

# ─────────────────────────────────────────────────────────────────────────────
#  CSV BAŞLIĞI
# ─────────────────────────────────────────────────────────────────────────────
"zaman,sure_sn,sorgu_no,durum,yanit_ms,R,G,B,wifi,meta,ham_yanit" |
    Out-File -FilePath $logPath -Encoding UTF8

# ─────────────────────────────────────────────────────────────────────────────
#  YARDIMCI: Süre biçimlendir
# ─────────────────────────────────────────────────────────────────────────────
function Format-Duration ([int]$totalSec) {
    "{0:D2}:{1:D2}:{2:D2}" -f ([int]($totalSec / 3600)), ([int](($totalSec % 3600) / 60)), ($totalSec % 60)
}

# ─────────────────────────────────────────────────────────────────────────────
#  YARDIMCI: PiColor satırını ayrıştır
#  Format: ts;type;mode;v1;v2;v3[;meta];wifi=X;user=Y
# ─────────────────────────────────────────────────────────────────────────────
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

    # Meta ve wifi alanlarını bul
    for ($i = 6; $i -lt $p.Count; $i++) {
        if ($p[$i] -like "wifi=*")  { $r.Wifi = $p[$i].Substring(5) }
        elseif ($p[$i] -like "user=*") { }  # yoksay
        elseif ($p[$i].Length -gt 0) { $r.Meta = $p[$i] }
    }

    $r.Valid = $true
    return $r
}

# ─────────────────────────────────────────────────────────────────────────────
#  YARDIMCI: TCP sorgusu yap
# ─────────────────────────────────────────────────────────────────────────────
function Invoke-PiColorQuery ([string]$cmd) {
    $res = [PSCustomObject]@{
        OK = $false; Ms = 0; Line = ""; ErrorMsg = ""
    }
    $sw     = [Diagnostics.Stopwatch]::StartNew()
    $client = $null

    try {
        $client = New-Object Net.Sockets.TcpClient
        $task   = $client.ConnectAsync($HostName, $Port)

        if (-not $task.Wait($Timeout * 1000)) {
            $res.ErrorMsg = "TIMEOUT (bağlantı $Timeout sn)"
            return $res
        }
        if (-not $client.Connected) {
            $res.ErrorMsg = "BAĞLANTI REDDEDİLDİ"
            return $res
        }

        $stream = $client.GetStream()
        $stream.ReadTimeout  = $Timeout * 1000
        $stream.WriteTimeout = $Timeout * 1000

        # Komut gönder
        $bytes = [Text.Encoding]::UTF8.GetBytes($cmd + "`n")
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()

        # İlk anlamlı satırı oku (";"-içeren, type=1/4/7 olan)
        $reader   = New-Object IO.StreamReader($stream, [Text.Encoding]::UTF8)
        $deadline = [DateTime]::Now.AddSeconds($Timeout)
        $found    = $false

        while ([DateTime]::Now -lt $deadline -and -not $found) {
            if ($client.Available -gt 0 -or $stream.DataAvailable) {
                $line = $reader.ReadLine()
                if ($line -and $line.Contains(";")) {
                    $res.Line = $line.Trim()
                    $res.OK   = $true
                    $res.Ms   = [int]$sw.ElapsedMilliseconds
                    $found    = $true
                }
            } else {
                Start-Sleep -Milliseconds 80
            }
        }

        if (-not $found) { $res.ErrorMsg = "TIMEOUT (yanıt $Timeout sn)" }

    } catch {
        $res.ErrorMsg = $_.Exception.Message -replace "`r`n"," "
    } finally {
        try { if ($client) { $client.Close() } } catch {}
        $sw.Stop()
    }

    return $res
}

# ─────────────────────────────────────────────────────────────────────────────
#  ÖZET RAPOR (Ctrl+C veya pil bitti)
# ─────────────────────────────────────────────────────────────────────────────
function Show-Summary ([string]$reason) {
    $endTime  = Get-Date
    $totalSec = [int]($endTime - $startTime).TotalSeconds

    # Pil ölüm zamanını bul (son başarılı yanıt)
    $pilOlumSure = if ($lastSuccessTime) {
        [int]($lastSuccessTime - $startTime).TotalSeconds
    } else { 0 }

    Write-Host ""
    Write-Host ""
    Write-Host ("  " + ("═" * 65)) -ForegroundColor DarkCyan
    Write-Host "  TEST ÖZETI — $reason" -ForegroundColor Cyan
    Write-Host ("  " + ("═" * 65)) -ForegroundColor DarkCyan
    Write-Host ""
    Write-Host ("  {0,-22} {1}" -f "Başlangıç:",   $startTime.ToString("dd.MM.yyyy HH:mm:ss")) -ForegroundColor White
    Write-Host ("  {0,-22} {1}" -f "Bitiş:",        $endTime.ToString("dd.MM.yyyy HH:mm:ss")) -ForegroundColor White
    Write-Host ("  {0,-22} {1}" -f "Test süresi:",  (Format-Duration $totalSec) + " ($totalSec sn)") -ForegroundColor White
    Write-Host ""

    if ($pilOlumSure -gt 0) {
        $pilStr = (Format-Duration $pilOlumSure) + " ($pilOlumSure sn)"
        $col    = if ($pilOlumSure -gt 7200) { "Green" } elseif ($pilOlumSure -gt 3600) { "Yellow" } else { "Red" }
        Write-Host ("  {0,-22} {1}" -f "PILİN DAYANIĞI SURE:", $pilStr) -ForegroundColor $col
        if ($lastSuccessTime) {
            Write-Host ("  {0,-22} {1}" -f "Son başarılı yanıt:", $lastSuccessTime.ToString("HH:mm:ss")) -ForegroundColor Yellow
        }
    }

    Write-Host ""
    Write-Host ("  {0,-22} {1}" -f "Toplam sorgu:",  $iteration) -ForegroundColor White
    Write-Host ("  {0,-22} {1}" -f "Başarılı:",      $successCount) -ForegroundColor Green
    Write-Host ("  {0,-22} {1}" -f "Başarısız:",     $failCount) -ForegroundColor $(if ($failCount -gt 0) { "Red" } else { "Gray" })

    $basariOrani = if ($iteration -gt 0) { [int](($successCount / $iteration) * 100) } else { 0 }
    Write-Host ("  {0,-22} {1}%" -f "Başarı oranı:", $basariOrani) -ForegroundColor $(if ($basariOrani -ge 90) { "Green" } else { "Yellow" })

    Write-Host ""
    Write-Host ("  {0,-22} {1}" -f "Log dosyası:", $logPath) -ForegroundColor Cyan
    Write-Host ("  " + ("═" * 65)) -ForegroundColor DarkCyan
    Write-Host ""

    # Log'a özet ekle
    $ozet = @"

# ─── TEST ÖZETI ───────────────────────────────────────────────────
# Sebep          : $reason
# Başlangıç      : $($startTime.ToString("dd.MM.yyyy HH:mm:ss"))
# Bitiş          : $($endTime.ToString("dd.MM.yyyy HH:mm:ss"))
# Pil ömrü (sn)  : $pilOlumSure
# Pil ömrü (saat): $(Format-Duration $pilOlumSure)
# Toplam sorgu   : $iteration
# Başarılı       : $successCount
# Başarısız      : $failCount
"@
    $ozet | Add-Content -Path $logPath -Encoding UTF8
}

# ─────────────────────────────────────────────────────────────────────────────
#  ANA DÖNGÜ
# ─────────────────────────────────────────────────────────────────────────────
try {
    while ($true) {
        $iteration++
        $elapsed = [int]((Get-Date) - $startTime).TotalSeconds
        $durStr  = Format-Duration $elapsed
        $saat    = (Get-Date).ToString("HH:mm:ss")

        # Her StatusInterval sorguda bir DURUM da gönder (WiFi güncel bilgisi)
        $sorguKomutu = $Command
        if ($StatusInterval -gt 0 -and ($iteration % $StatusInterval) -eq 0) {
            $sorguKomutu = "DURUM"
        }

        $q = Invoke-PiColorQuery $sorguKomutu

        # ── Konsol çıktısı ────────────────────────────────────────────────────
        $sorguStr = "#{0:D4}" -f $iteration

        if ($q.OK) {
            $p = Parse-Line $q.Line
            $successCount++
            $consecutiveFail = 0
            $lastSuccessTime = Get-Date
            if ($p.Wifi) { $lastWifi = $p.Wifi }

            $wifiRenk = switch -Wildcard ($lastWifi) {
                "AP"     { "DarkYellow" }
                ""       { "DarkGray"   }
                default  { "Cyan"       }  # SSID adı = STA bağlı
            }

            Write-Host ("  {0}  {1}  {2}  " -f $saat, $durStr, $sorguStr) -NoNewline -ForegroundColor DarkGray
            Write-Host ("✓ {0,4}ms  " -f $q.Ms) -NoNewline -ForegroundColor Green

            if ($p.Valid -and $p.Type -ne "4") {
                Write-Host "R:" -NoNewline -ForegroundColor Red
                Write-Host ("{0,7}  " -f $p.V1) -NoNewline -ForegroundColor White
                Write-Host "G:" -NoNewline -ForegroundColor Green
                Write-Host ("{0,7}  " -f $p.V2) -NoNewline -ForegroundColor White
                Write-Host "B:" -NoNewline -ForegroundColor Blue
                Write-Host ("{0,7}  " -f $p.V3) -NoNewline -ForegroundColor White
            } else {
                Write-Host ("{0,-26}" -f $p.Meta.Substring(0, [Math]::Min(25, $p.Meta.Length))) -NoNewline -ForegroundColor DarkGray
            }

            Write-Host $lastWifi -ForegroundColor $wifiRenk

            # CSV'ye yaz (ham yanıtta çift tırnak varsa temizle)
            $csvSatir = "{0},{1},{2},OK,{3},{4},{5},{6},{7},{8},{9}" -f `
                $saat, $elapsed, $iteration, $q.Ms,
                $p.V1, $p.V2, $p.V3, $lastWifi, $p.Meta,
                ('"' + $q.Line.Replace('"', "'") + '"')
            $csvSatir | Add-Content -Path $logPath -Encoding UTF8

        } else {
            $failCount++
            $consecutiveFail++

            Write-Host ("  {0}  {1}  {2}  " -f $saat, $durStr, $sorguStr) -NoNewline -ForegroundColor DarkGray
            Write-Host ("✗ HATA  " ) -NoNewline -ForegroundColor Red
            Write-Host $q.ErrorMsg -ForegroundColor DarkRed

            # CSV'ye hata yaz
            $csvSatir = "{0},{1},{2},HATA,0,,,,{3},{4},{5}" -f `
                $saat, $elapsed, $iteration, $lastWifi, $q.ErrorMsg, '""'
            $csvSatir | Add-Content -Path $logPath -Encoding UTF8

            # Arka arkaya MaxConsecutiveFail hata → pil bitti uyarısı
            if ($consecutiveFail -ge $MaxConsecutiveFail) {
                Write-Host ""
                Write-Host ("  [!] {0} arka arkaya bağlantı hatası → cihaz yanıt vermiyor." -f $consecutiveFail) -ForegroundColor Red
                $pilOmruSn = if ($lastSuccessTime) {
                    [int]($lastSuccessTime - $startTime).TotalSeconds
                } else { 0 }
                if ($pilOmruSn -gt 0) {
                    Write-Host ("  [!] Pil ömrü tahmini: {0}  ({1} saniye)" -f (Format-Duration $pilOmruSn), $pilOmruSn) -ForegroundColor Yellow
                } else {
                    Write-Host "  [!] Cihaza hiç ulaşılamadı — IP/port/WiFi ayarını kontrol edin." -ForegroundColor Red
                }
                $batteryDeadTime = Get-Date
                break
            }
        }

        # ── Sonraki sorgुya kadar say-geri göster ────────────────────────────
        for ($s = $Interval; $s -gt 0; $s--) {
            Write-Host -NoNewline ("`r  Sonraki sorgu: {0,3} sn ... " -f $s) -ForegroundColor DarkGray
            Start-Sleep -Seconds 1
        }
        Write-Host -NoNewline ("`r" + (" " * 35) + "`r")  # satırı temizle

    }  # while
} finally {
    $sebep = if ($batteryDeadTime) { "CİHAZ YANIT VERMİYOR (pil bitti?)" } else { "Kullanıcı durdurdu (Ctrl+C)" }
    Show-Summary $sebep
}
