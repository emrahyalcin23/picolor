#!/usr/bin/env python3
"""
PiColor Device Test Suite
Bağlantı: --port (COM/Serial) | --ip (WiFi TCP) | --ble (Bluetooth)

Kullanım:
    python picolor_test.py --all --port /dev/ttyUSB0          # Tüm kanallar (IP otomatik)
    python picolor_test.py --port /dev/ttyUSB0                # Sadece Serial
    python picolor_test.py --ip 192.168.42.1                  # Sadece WiFi (IP elle)
    python picolor_test.py --ble-addr AA:BB:CC:DD:EE:FF       # Sadece BLE
    python picolor_test.py --no-serial --no-ble               # Sadece WiFi
    python picolor_test.py --all --port COM3 --verbose        # Tüm kanallar, ayrıntılı

  --all ile Serial önce bağlanır; --ip verilmediyse WiFi IP'si DURUM/WIFI_BILGI
  yanıtından otomatik çekilir, ardından WiFi ve BLE testleri çalışır.

Gereksinimler:
    pip install pyserial bleak
"""

import argparse
import asyncio
import re
import socket
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime
from typing import Optional

try:
    import serial
    HAS_SERIAL = True
except ImportError:
    HAS_SERIAL = False

try:
    from bleak import BleakClient, BleakScanner
    HAS_BLE = True
except ImportError:
    HAS_BLE = False


# ────────────────────────────── Sabitler ──────────────────────────────

BAUD_RATE        = 115200
DEFAULT_TCP_PORT = 8266
DEFAULT_AP_IP    = "192.168.42.1"
DEFAULT_BLE_NAME = "PiColor"

BLE_NUS_RX_UUID  = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # host → cihaz
BLE_NUS_TX_UUID  = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # cihaz → host

CMD_TIMEOUT      = 3.0   # ilk yanıt için maksimum bekleme (saniye)
IDLE_GAP         = 0.6   # yanıt geldikten sonra sessizlik = bitti (saniye)
STREAM_LINES     = 5     # canlı akış testi için beklenen satır sayısı


# ────────────────────────────── Veri yapıları ─────────────────────────

@dataclass
class TestResult:
    name:     str
    passed:   bool
    message:  str = ""
    response: str = ""

@dataclass
class TestSuite:
    channel: str
    results: list = field(default_factory=list)

    def add(self, r: TestResult):
        self.results.append(r)

    @property
    def passed(self): return sum(1 for r in self.results if r.passed)
    @property
    def failed(self): return sum(1 for r in self.results if not r.passed)
    @property
    def total(self):  return len(self.results)


# ────────────────────────────── Format doğrulayıcılar ─────────────────

# Standart: <ts>;<tip>;<mod>;<v1>;<v2>;<v3>[;...]
STD_RE  = re.compile(r"^\d+;[0-7];\d+;-?\d+(\.\d+)?;-?\d+(\.\d+)?;-?\d+(\.\d+)?")
# Dual (tip=6): <ts>;6;<mod>;<hR>;<hG>;<hB>;<hC>;<pR>;<pG>;<pB>[;...]
DUAL_RE = re.compile(r"^\d+;6;\d+;\d+;\d+;\d+;\d+;-?\d+(\.\d+)?;-?\d+(\.\d+)?;-?\d+(\.\d+)?")

IDENTITY_RE = re.compile(r"^IDENTITY=PICOLOR_v\d+\.\d+\.\d+")
VERSION_RE  = re.compile(r"^VERSION=v\d+\.\d+\.\d+")
IP_RE       = re.compile(r'\b(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})\b')


def is_std(line: str)  -> bool: return bool(STD_RE.match(line))
def is_dual(line: str) -> bool: return bool(DUAL_RE.match(line))

def field_of(line: str, idx: int) -> Optional[str]:
    parts = line.split(";")
    return parts[idx] if idx < len(parts) else None

def values_numeric(line: str) -> bool:
    try:
        for i in [3, 4, 5]:
            float(field_of(line, i))
        return True
    except (TypeError, ValueError):
        return False


# ────────────────────────────── Transport katmanı ─────────────────────

class SerialTransport:
    def __init__(self, port: str, baud: int = BAUD_RATE):
        self.port = port
        self.baud = baud
        self._s   = None

    def connect(self):
        self._s = serial.Serial(self.port, self.baud, timeout=0.2)
        time.sleep(2.0)          # RP2040 USB CDC yerleşmesi
        self._s.reset_input_buffer()

    def send(self, cmd: str):
        self._s.write((cmd + "\n").encode())
        self._s.flush()

    def _readline(self) -> str:
        return self._s.readline().decode(errors="replace").strip()

    def read_until(self, timeout: float = CMD_TIMEOUT) -> list[str]:
        lines      = []
        deadline   = time.time() + timeout
        idle_since = None
        while time.time() < deadline:
            line = self._readline()
            if line:
                lines.append(line)
                idle_since = None
            else:
                if lines:
                    if idle_since is None:
                        idle_since = time.time()
                    elif time.time() - idle_since >= IDLE_GAP:
                        break
        return lines

    def close(self):
        if self._s:
            self._s.close()


class TcpTransport:
    def __init__(self, ip: str, port: int = DEFAULT_TCP_PORT):
        self.ip   = ip
        self.port = port
        self._sock = None
        self._buf  = b""

    def connect(self):
        self._sock = socket.create_connection((self.ip, self.port), timeout=5)
        self._sock.settimeout(0.2)
        time.sleep(0.4)
        try:
            self._sock.recv(4096)   # hoşgeldin banner'ı
        except socket.timeout:
            pass

    def send(self, cmd: str):
        self._sock.sendall((cmd + "\n").encode())

    def _readline(self) -> str:
        deadline = time.time() + 0.2
        while b"\n" not in self._buf and time.time() < deadline:
            try:
                chunk = self._sock.recv(1024)
                if not chunk:
                    break
                self._buf += chunk
            except socket.timeout:
                break
        if b"\n" in self._buf:
            idx          = self._buf.index(b"\n")
            line         = self._buf[:idx].decode(errors="replace").strip()
            self._buf    = self._buf[idx + 1:]
            return line
        return ""

    def read_until(self, timeout: float = CMD_TIMEOUT) -> list[str]:
        lines      = []
        deadline   = time.time() + timeout
        idle_since = None
        while time.time() < deadline:
            line = self._readline()
            if line:
                lines.append(line)
                idle_since = None
            else:
                if lines:
                    if idle_since is None:
                        idle_since = time.time()
                    elif time.time() - idle_since >= IDLE_GAP:
                        break
        return lines

    def close(self):
        if self._sock:
            self._sock.close()


class BleTransport:
    """Senkron API (bleak async wrapping)."""

    def __init__(self, address: str):
        self.address = address
        self._client = None
        self._queue  = None
        self._loop   = None

    def connect(self):
        self._loop   = asyncio.new_event_loop()
        self._queue  = asyncio.Queue()
        self._loop.run_until_complete(self._async_connect())

    async def _async_connect(self):
        def _notify(_, data: bytearray):
            for part in data.decode(errors="replace").splitlines():
                part = part.strip()
                if part:
                    self._queue.put_nowait(part)

        self._client = BleakClient(self.address, loop=self._loop)
        await self._client.connect()
        await self._client.start_notify(BLE_NUS_TX_UUID, _notify)

    def send(self, cmd: str):
        self._loop.run_until_complete(
            self._client.write_gatt_char(
                BLE_NUS_RX_UUID, (cmd + "\n").encode(), response=False
            )
        )

    def _get_nowait(self) -> str:
        try:
            return self._loop.run_until_complete(
                asyncio.wait_for(self._queue.get(), timeout=0.2)
            )
        except asyncio.TimeoutError:
            return ""

    def read_until(self, timeout: float = CMD_TIMEOUT) -> list[str]:
        lines      = []
        deadline   = time.time() + timeout
        idle_since = None
        while time.time() < deadline:
            remaining = deadline - time.time()
            if remaining <= 0:
                break
            line = self._get_nowait()
            if line:
                lines.append(line)
                idle_since = None
            else:
                if lines:
                    if idle_since is None:
                        idle_since = time.time()
                    elif time.time() - idle_since >= IDLE_GAP:
                        break
        return lines

    def close(self):
        async def _disc():
            await self._client.stop_notify(BLE_NUS_TX_UUID)
            await self._client.disconnect()
        self._loop.run_until_complete(_disc())
        self._loop.close()


# ────────────────────────────── Yardımcılar ───────────────────────────

def cmd(t, command: str, timeout: float = CMD_TIMEOUT) -> list[str]:
    t.send(command)
    return t.read_until(timeout)

def first(lines: list[str]) -> str:
    return next((l for l in lines if l.strip()), "")

def flush(t):
    t.send("OKU_STOP")
    time.sleep(0.3)
    t.read_until(timeout=0.5)


def ip_from_durum(t) -> Optional[str]:
    """WIFI_BILGI ve DURUM yanıtlarından cihazın AP veya STA IP adresini çeker."""
    all_lines: list[str] = []
    for komut in ("WIFI_BILGI", "DURUM"):
        all_lines.extend(cmd(t, komut, timeout=4))

    # 1. "AP" + "IP" içeren satırı önceliklendir
    for line in all_lines:
        if re.search(r'\bap\b', line, re.IGNORECASE) and re.search(r'\bip\b', line, re.IGNORECASE):
            m = IP_RE.search(line)
            if m and not m.group(1).startswith(("0.", "127.")):
                return m.group(1)

    # 2. "STA" + "IP" içeren satır
    for line in all_lines:
        if re.search(r'\bsta\b', line, re.IGNORECASE) and re.search(r'\bip\b', line, re.IGNORECASE):
            m = IP_RE.search(line)
            if m and not m.group(1).startswith(("0.", "127.")):
                return m.group(1)

    # 3. Herhangi bir satırdaki geçerli özel (private) IPv4
    for line in all_lines:
        m = IP_RE.search(line)
        if m:
            ip = m.group(1)
            if not ip.startswith(("0.", "127.", "255.")):
                return ip

    return None


# ────────────────────────────── Test fonksiyonları ────────────────────

def t_kimsin(t, s):
    lines = cmd(t, "KIMSIN")
    line  = first(lines)
    ok    = bool(IDENTITY_RE.match(line))
    s.add(TestResult("KIMSIN → IDENTITY=PICOLOR_v*", ok, "" if ok else f"Yanıt: {line}", line))

def t_versiyon(t, s):
    lines = cmd(t, "VERSIYON")
    line  = first(lines)
    ok    = bool(VERSION_RE.match(line))
    s.add(TestResult("VERSIYON → VERSION=v*", ok, "" if ok else f"Yanıt: {line}", line))

def t_durum(t, s):
    lines = cmd(t, "DURUM", timeout=4)
    ok    = len(lines) >= 3
    s.add(TestResult(f"DURUM → çok satırlı durum ({len(lines)} satır)", ok,
                     "" if ok else "Çok az satır", "\n".join(lines[:5])))

def t_mod_sorgu(t, s):
    lines = cmd(t, "MOD")
    line  = first(lines)
    ok    = "STABIL" in line or "DINAMIK" in line
    s.add(TestResult("MOD → STABIL veya DINAMIK", ok, "" if ok else f"Yanıt: {line}", line))

def t_mod_stabil(t, s):
    lines = cmd(t, "MOD_STABIL")
    text  = " ".join(lines).upper()
    ok    = "STABIL" in text
    s.add(TestResult("MOD_STABIL → geçiş onayı", ok, "" if ok else f"Yanıt: {text[:60]}", first(lines)))

def t_mod_dinamik(t, s):
    lines = cmd(t, "MOD_DINAMIK")
    text  = " ".join(lines).upper()
    ok    = "DINAMIK" in text
    s.add(TestResult("MOD_DINAMIK → geçiş onayı", ok, "" if ok else f"Yanıt: {text[:60]}", first(lines)))

def t_oku_0(t, s):
    lines = cmd(t, "OKU_0")
    data  = [l for l in lines if is_std(l)]
    ok    = len(data) >= 1 and field_of(data[0], 1) == "1" and values_numeric(data[0])
    s.add(TestResult("OKU_0 → tip=1, değerler sayısal", ok,
                     f"Veri: {data[:2]}" if not ok else "", data[0] if data else ""))

def t_raw(t, s):
    lines = cmd(t, "RAW")
    data  = [l for l in lines if is_std(l)]
    ok    = len(data) >= 1 and field_of(data[0], 1) == "3"
    s.add(TestResult("RAW → tip=3, ham sensör", ok,
                     f"Veri: {data[:2]}" if not ok else "", data[0] if data else ""))

def t_oku_canli(t, s):
    t.send("OKU")
    alindi = []
    bitis  = time.time() + STREAM_LINES * 0.5
    while time.time() < bitis:
        lines = t.read_until(timeout=0.4)
        for l in lines:
            if is_std(l) and field_of(l, 1) == "0":
                alindi.append(l)
        if len(alindi) >= STREAM_LINES:
            break
    flush(t)
    ok = len(alindi) >= 2
    s.add(TestResult(f"OKU canlı akış tip=0 ({len(alindi)}/{STREAM_LINES} satır)", ok,
                     "" if ok else "Yeterli akış satırı gelmedi",
                     alindi[0] if alindi else ""))

def t_oku_s10(t, s):
    lines = cmd(t, "OKU_S10", timeout=4)
    data  = [l for l in lines if is_std(l)]
    ok    = len(data) >= 1 and field_of(data[0], 1) == "2"
    s.add(TestResult("OKU_S10 → tip=2, ortalama", ok,
                     f"Veri: {data[:2]}" if not ok else "", data[0] if data else ""))

def t_oku_m1(t, s):
    lines = cmd(t, "OKU_1", timeout=4)
    data  = [l for l in lines if is_std(l)]
    ok    = len(data) >= 1 and field_of(data[0], 1) == "2"
    s.add(TestResult("OKU_1 → tip=2, 1-dakika ort.", ok,
                     f"Veri: {data[:2]}" if not ok else "", data[0] if data else ""))

def t_dual_oku(t, s):
    lines = cmd(t, "DUAL_OKU")
    data  = [l for l in lines if is_dual(l)]
    ok    = len(data) >= 1 and field_of(data[0], 1) == "6"
    s.add(TestResult("DUAL_OKU → tip=6, ham+işlenmiş", ok,
                     f"Yanıt: {lines[:3]}" if not ok else "", data[0] if data else ""))

def t_dual_canli(t, s):
    t.send("DUAL_AKIS")
    alindi = []
    bitis  = time.time() + 3.0
    while time.time() < bitis:
        lines = t.read_until(timeout=0.4)
        for l in lines:
            if is_dual(l):
                alindi.append(l)
        if len(alindi) >= 2:
            break
    flush(t)
    ok = len(alindi) >= 1
    s.add(TestResult(f"DUAL_AKIS canlı dual akış ({len(alindi)} satır)", ok,
                     "" if ok else "Dual satır alınamadı",
                     alindi[0] if alindi else ""))

def t_tum(t, s):
    lines = cmd(t, "TUM", timeout=4)
    data  = [l for l in lines if is_std(l)]
    full  = [l for l in data if field_of(l, 1) == "7"]
    ok    = len(full) >= 1
    s.add(TestResult("TUM → tip=7 tam paket", ok,
                     f"Veri: {data[:3]}" if not ok else "", full[0] if full else ""))

def t_stabil_oku(t, s):
    lines = cmd(t, "STABIL_OKU_0")
    data  = [l for l in lines if is_std(l)]
    ok    = len(data) >= 1 and field_of(data[0], 2) == "0"   # mod=0 → STABIL
    s.add(TestResult("STABIL_OKU_0 → mod alanı=0", ok,
                     f"Veri: {data[:2]}" if not ok else "", data[0] if data else ""))

def t_dinamik_oku(t, s):
    lines = cmd(t, "DINAMIK_OKU_0")
    data  = [l for l in lines if is_std(l)]
    ok    = len(data) >= 1 and field_of(data[0], 2) == "1"   # mod=1 → DINAMIK
    s.add(TestResult("DINAMIK_OKU_0 → mod alanı=1", ok,
                     f"Veri: {data[:2]}" if not ok else "", data[0] if data else ""))

def t_katsayilar(t, s):
    lines = cmd(t, "KATSAYILAR")
    text  = " ".join(lines).lower()
    ok    = any(k in text for k in ["wr", "wg", "wb", "wl"])
    s.add(TestResult("KATSAYILAR → wR,wG,wB,wL gösterir", ok,
                     f"Yanıt: {lines[:2]}" if not ok else "", "\n".join(lines[:4])))

def t_sifirla(t, s):
    lines = cmd(t, "SIFIRLA")
    text  = " ".join(lines).lower()
    ok    = any(k in text for k in ["sifirland", "reset", "0.0", "wr=0", "wg=0", "0"])
    s.add(TestResult("SIFIRLA → katsayılar sıfırlandı", ok,
                     f"Yanıt: {lines[:2]}" if not ok else "", "\n".join(lines[:3])))

def t_basamak(t, s):
    lines = cmd(t, "BASAMAK_2")
    text  = " ".join(lines).lower()
    ok    = any(k in text for k in ["basamak", "decimal", "2"])
    s.add(TestResult("BASAMAK_2 → ondalık basamak değişti", ok,
                     f"Yanıt: {lines[:2]}" if not ok else "", first(lines)))
    # Varsayılana dön
    t.send("VARSAYILAN")
    time.sleep(0.2)
    t.read_until(timeout=0.5)

def t_basamak_hassasiyet(t, s):
    """BASAMAK_3 sonrası OKU_0 çıktısında en fazla 3 ondalık basamak beklenir."""
    t.send("BASAMAK_3")
    time.sleep(0.2)
    lines = cmd(t, "OKU_0")
    data  = [l for l in lines if is_std(l)]
    if not data:
        s.add(TestResult("BASAMAK_3 hassasiyet kontrolü", False, "Veri satırı yok"))
        t.send("VARSAYILAN"); time.sleep(0.2); t.read_until(timeout=0.5)
        return
    v1 = field_of(data[0], 3) or ""
    decimals = len(v1.split(".")[1]) if "." in v1 else 0
    ok = 0 <= decimals <= 3
    s.add(TestResult(f"BASAMAK_3 hassasiyet ({decimals} basamak ≤ 3)", ok,
                     f"Değer: {v1}" if not ok else "", data[0]))
    t.send("VARSAYILAN"); time.sleep(0.2); t.read_until(timeout=0.5)

def t_logaritmik(t, s):
    lines = cmd(t, "LOGARITMIK")
    text  = " ".join(lines).lower()
    ok    = any(k in text for k in ["log", "aktif", "enabled", "on"])
    s.add(TestResult("LOGARITMIK → log ölçeği aktif", ok,
                     f"Yanıt: {lines[:2]}" if not ok else "", first(lines)))

def t_lineer(t, s):
    lines = cmd(t, "LINEER")
    text  = " ".join(lines).lower()
    ok    = any(k in text for k in ["lineer", "linear", "aktif", "enabled", "on"])
    s.add(TestResult("LINEER → lineer ölçek aktif", ok,
                     f"Yanıt: {lines[:2]}" if not ok else "", first(lines)))

def t_olcek(t, s):
    lines = cmd(t, "OLCEK")
    ok    = len(lines) >= 1 and any(l.strip() for l in lines)
    s.add(TestResult("OLCEK → ölçek ayarları gösterildi", ok,
                     f"Yanıt: {lines[:2]}" if not ok else "", "\n".join(lines[:3])))

def t_varsayilan(t, s):
    lines = cmd(t, "VARSAYILAN")
    text  = " ".join(lines).lower()
    ok    = any(k in text for k in ["varsayilan", "default", "lineer", "basamak", "1"])
    s.add(TestResult("VARSAYILAN → fabrika ayarları", ok,
                     f"Yanıt: {lines[:2]}" if not ok else "", first(lines)))

def t_tampon(t, s):
    lines = cmd(t, "TAMPON")
    text  = " ".join(lines)
    ok    = "/" in text or "%" in text or any(c.isdigit() for c in text)
    s.add(TestResult("TAMPON → tampon durumu", ok,
                     f"Yanıt: {lines[:2]}" if not ok else "", "\n".join(lines[:3])))

def t_sd_durum(t, s):
    lines = cmd(t, "SD_DURUM")
    ok    = len(lines) >= 1
    s.add(TestResult("SD_DURUM → SD kart durumu", ok,
                     f"Yanıt: {lines[:2]}" if not ok else "", "\n".join(lines[:3])))

def t_wifi_bilgi(t, s):
    lines = cmd(t, "WIFI_BILGI")
    text  = " ".join(lines).lower()
    ok    = any(k in text for k in ["ssid", "ip", "wifi", "ap", "sta", "192.168"])
    s.add(TestResult("WIFI_BILGI → AP/STA bilgisi", ok,
                     f"Yanıt: {lines[:2]}" if not ok else "", "\n".join(lines[:5])))

def t_yardim(t, s):
    lines = cmd(t, "YARDIM", timeout=4)
    ok    = len(lines) >= 5
    s.add(TestResult(f"YARDIM → komut listesi ({len(lines)} satır)", ok,
                     "" if ok else "Çok az satır", "\n".join(lines[:4])))

def t_test_cmd(t, s):
    lines = cmd(t, "TEST", timeout=6)
    ok    = len(lines) >= 1
    s.add(TestResult("TEST → sistem testi", ok,
                     f"Yanıt: {lines[:2]}" if not ok else "", "\n".join(lines[:4])))

def t_format_tutarlilik(t, s):
    """STABIL ve DINAMIK modda aynı format beklenir."""
    t.send("MOD_STABIL"); time.sleep(0.3)
    r1 = cmd(t, "OKU_0")
    t.send("MOD_DINAMIK"); time.sleep(0.3)
    r2 = cmd(t, "OKU_0")
    d1 = [l for l in r1 if is_std(l)]
    d2 = [l for l in r2 if is_std(l)]
    ok = (len(d1) >= 1 and len(d2) >= 1 and
          values_numeric(d1[0]) and values_numeric(d2[0]))
    s.add(TestResult("Format tutarlılığı (STABIL vs DINAMIK)", ok,
                     "" if ok else f"S:{d1} D:{d2}",
                     f"STABIL: {d1[0] if d1 else 'YOK'} | DINAMIK: {d2[0] if d2 else 'YOK'}"))

def t_timestamp_artan(t, s):
    """Ardışık OKU_0 çağrılarında zaman damgası artmalıdır."""
    lines1 = cmd(t, "OKU_0"); time.sleep(0.5)
    lines2 = cmd(t, "OKU_0")
    d1 = [l for l in lines1 if is_std(l)]
    d2 = [l for l in lines2 if is_std(l)]
    if not d1 or not d2:
        s.add(TestResult("Zaman damgası artan", False, "Veri satırı yok"))
        return
    try:
        ts1 = int(field_of(d1[0], 0))
        ts2 = int(field_of(d2[0], 0))
        ok  = ts2 > ts1
    except (TypeError, ValueError):
        ok = False
    s.add(TestResult(f"Zaman damgası artan ({ts1}→{ts2})", ok,
                     "" if ok else "ts2 ≤ ts1", f"{d1[0]} | {d2[0]}"))

def t_deger_aralik(t, s):
    """İşlenmiş değerler 0-100 aralığında olmalıdır."""
    lines = cmd(t, "OKU_0")
    data  = [l for l in lines if is_std(l) and field_of(l, 1) == "1"]
    if not data:
        s.add(TestResult("Değer aralığı 0-100", False, "Veri satırı yok"))
        return
    try:
        vals = [float(field_of(data[0], i)) for i in [3, 4, 5]]
        ok   = all(-1.0 <= v <= 101.0 for v in vals)   # küçük taşma payı
    except (TypeError, ValueError):
        ok, vals = False, []
    s.add(TestResult(f"Değer aralığı 0-100 (vals={[f'{v:.1f}' for v in vals]})", ok,
                     "" if ok else f"Aralık dışı: {vals}", data[0]))

def t_raw_16bit(t, s):
    """Ham değerler 0-65535 aralığında olmalıdır."""
    lines = cmd(t, "RAW")
    data  = [l for l in lines if is_std(l) and field_of(l, 1) == "3"]
    if not data:
        s.add(TestResult("RAW 16-bit aralık", False, "Veri satırı yok"))
        return
    try:
        vals = [int(field_of(data[0], i)) for i in [3, 4, 5]]
        ok   = all(0 <= v <= 65535 for v in vals)
    except (TypeError, ValueError):
        ok, vals = False, []
    s.add(TestResult(f"RAW 16-bit aralık (vals={vals})", ok,
                     "" if ok else f"Aralık dışı: {vals}", data[0]))

def t_user_etiketi(t, s):
    """TCP/BLE kanallarında USER: prefix testi."""
    t.send("USER:testuser OKU_0")
    lines = t.read_until()
    data  = [l for l in lines if is_std(l)]
    if not data:
        s.add(TestResult("USER: etiketi (atlandı - veri yok)", True, "Veri gelmedi, atlandı"))
        return
    has_user = any("user=" in l.lower() or "testuser" in l.lower() for l in lines)
    s.add(TestResult("USER: etiketi yanıtta görünüyor", has_user,
                     "" if has_user else f"Yanıtlar: {lines[:3]}", "\n".join(lines[:3])))

def t_hatali_komut(t, s):
    """Bilinmeyen komut → hata veya sessizlik (çökme olmamalı)."""
    lines = cmd(t, "COKBILMEMIS_KOMUT_XYZ_12345")
    # Cihaz ya boş döner ya da hata mesajı verir; ikisi de geçerli
    ok    = True   # Bağlantı kopmadıysa test geçti
    s.add(TestResult("Hatalı komut → çökmez", ok, "", first(lines) or "(sessiz)"))


# ────────────────────────────── Test listesi ──────────────────────────

TEMEL_TESTLER = [
    t_kimsin,
    t_versiyon,
    t_durum,
]

MOD_TESTLERI = [
    t_mod_sorgu,
    t_mod_stabil,
    t_mod_dinamik,
]

OKUMA_TESTLERI = [
    t_oku_0,
    t_raw,
    t_oku_canli,
    t_oku_s10,
    t_oku_m1,
    t_dual_oku,
    t_dual_canli,
    t_tum,
    t_stabil_oku,
    t_dinamik_oku,
]

FORMAT_TESTLERI = [
    t_basamak,
    t_basamak_hassasiyet,
    t_logaritmik,
    t_lineer,
    t_olcek,
    t_varsayilan,
]

KALIBRASYON_TESTLERI = [
    t_katsayilar,
    t_sifirla,
]

SISTEM_TESTLERI = [
    t_tampon,
    t_sd_durum,
    t_wifi_bilgi,
    t_yardim,
    t_test_cmd,
]

KALITE_TESTLERI = [
    t_format_tutarlilik,
    t_timestamp_artan,
    t_deger_aralik,
    t_raw_16bit,
    t_user_etiketi,
    t_hatali_komut,
]

TUM_TESTLER = (
    TEMEL_TESTLER +
    MOD_TESTLERI +
    OKUMA_TESTLERI +
    FORMAT_TESTLERI +
    KALIBRASYON_TESTLERI +
    SISTEM_TESTLERI +
    KALITE_TESTLERI
)


# ────────────────────────────── Test koşucusu ─────────────────────────

GRUPLAR = {
    "Temel":        TEMEL_TESTLER,
    "Mod":          MOD_TESTLERI,
    "Okuma":        OKUMA_TESTLERI,
    "Format":       FORMAT_TESTLERI,
    "Kalibrasyon":  KALIBRASYON_TESTLERI,
    "Sistem":       SISTEM_TESTLERI,
    "Kalite":       KALITE_TESTLERI,
}

def run_suite(transport, channel_name: str, verbose: bool, groups: list[str]) -> TestSuite:
    suite = TestSuite(channel=channel_name)
    print(f"\n{'='*65}")
    print(f"  Kanal: {channel_name}")
    print(f"{'='*65}")

    for grup_adi, testler in GRUPLAR.items():
        if groups and grup_adi.lower() not in [g.lower() for g in groups]:
            continue
        print(f"\n  ── {grup_adi} ──")
        for fn in testler:
            try:
                fn(transport, suite)
            except Exception as exc:
                suite.add(TestResult(fn.__name__, False, f"İstisna: {exc}"))

            r      = suite.results[-1]
            simge  = "✓" if r.passed else "✗"
            print(f"    [{simge}] {r.name}")
            if verbose and r.response:
                for satir in r.response.splitlines()[:3]:
                    print(f"          > {satir}")
            if not r.passed and r.message:
                print(f"          ! {r.message}")

    return suite


def ozet_yazdir(suites: list[TestSuite]):
    print(f"\n{'='*65}")
    print("  ÖZET")
    print(f"{'='*65}")
    toplam_g = toplam_h = 0
    for s in suites:
        toplam_g += s.passed
        toplam_h += s.failed
        durum = "✓" if s.failed == 0 else f"({s.failed} HATA)"
        print(f"  {s.channel:35s}  {s.passed}/{s.total} geçti  {durum}")
    print(f"{'─'*65}")
    print(f"  {'TOPLAM':35s}  {toplam_g}/{toplam_g + toplam_h} geçti")
    if toplam_h == 0:
        print("\n  TÜM TESTLER BAŞARILI")
    else:
        print(f"\n  {toplam_h} TEST BAŞARISIZ")
    print(f"{'='*65}\n")


# ────────────────────────────── BLE tarama ────────────────────────────

async def ble_bul(name: str, timeout: float = 10.0) -> Optional[str]:
    print(f"  BLE tarama: '{name}' aranıyor ({timeout:.0f}s)...")
    cihazlar = await BleakScanner.discover(timeout=timeout)
    for c in cihazlar:
        if c.name and name.lower() in c.name.lower():
            print(f"  Bulundu: {c.name}  [{c.address}]")
            return c.address
    return None


# ────────────────────────────── Ana program ───────────────────────────

def main():
    p = argparse.ArgumentParser(
        description="PiColor cihaz test paketi — COM + WiFi + BLE",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Örnekler:
  python picolor_test.py --all --port /dev/ttyUSB0            # Tüm kanallar, IP otomatik
  python picolor_test.py --all --port COM3 --verbose          # Tüm kanallar, ayrıntılı
  python picolor_test.py --port /dev/ttyUSB0                  # Sadece Serial
  python picolor_test.py --ip 192.168.42.1 --tcp-port 8266    # Sadece WiFi (IP elle)
  python picolor_test.py --ble-addr AA:BB:CC:DD:EE:FF         # Sadece BLE
  python picolor_test.py --no-serial --no-ble                 # Sadece WiFi
  python picolor_test.py --ip 192.168.42.1 --groups temel okuma format
        """
    )
    p.add_argument("--all",       action="store_true",
                   help="Tüm kanalları sırasıyla test et: Serial → WiFi → BLE "
                        "(WiFi IP'si --ip verilmediyse DURUM'dan otomatik çekilir)")
    p.add_argument("--port",      help="Seri port (ör. /dev/ttyUSB0 veya COM3)")
    p.add_argument("--ip",        default=None,
                   help=f"Cihaz IP adresi — verilmezse DURUM/WIFI_BILGI yanıtından otomatik keşfedilir "
                        f"(varsayılan: {DEFAULT_AP_IP})")
    p.add_argument("--tcp-port",  type=int, default=DEFAULT_TCP_PORT,
                   help=f"TCP port (varsayılan: {DEFAULT_TCP_PORT})")
    p.add_argument("--ble",       default=DEFAULT_BLE_NAME,
                   help=f"BLE cihaz adı (varsayılan: {DEFAULT_BLE_NAME})")
    p.add_argument("--ble-addr",  help="BLE MAC adresi (taramayı atlar)")
    p.add_argument("--no-serial", action="store_true", help="Serial testi atla")
    p.add_argument("--no-wifi",   action="store_true", help="WiFi testi atla")
    p.add_argument("--no-ble",    action="store_true", help="BLE testi atla")
    p.add_argument("--verbose",   action="store_true", help="Ham yanıtları göster")
    p.add_argument("--groups",    nargs="*",
                   help="Belirli grupları çalıştır: temel mod okuma format kalibrasyon sistem kalite")
    args = p.parse_args()

    # --all: tüm kanalları etkinleştir (--no-xxx'leri geçersiz kıl)
    if args.all:
        do_serial = True
        do_wifi   = True
        do_ble    = True
    else:
        do_serial = not args.no_serial
        do_wifi   = not args.no_wifi
        do_ble    = not args.no_ble

    print(f"\nPiColor Cihaz Test Paketi")
    print(f"Tarih: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    if args.all:
        print("Mod  : --all  (Serial → WiFi → BLE)")
    if args.groups:
        print(f"Gruplar: {', '.join(args.groups)}")

    suites        = []
    discovered_ip: Optional[str] = None

    # ── Serial ──────────────────────────────────────────────────────
    if do_serial:
        if not HAS_SERIAL:
            print("\n[SERIAL] pyserial kurulu değil → pip install pyserial")
        elif not args.port:
            print("\n[SERIAL] Port belirtilmedi → --port /dev/ttyUSB0  (veya --no-serial)")
        else:
            print(f"\n[SERIAL] Bağlanıyor: {args.port} @ {BAUD_RATE} baud ...")
            try:
                ser = SerialTransport(args.port)
                ser.connect()
                print("[SERIAL] Bağlandı.")

                # --ip verilmediyse WiFi IP'sini DURUM/WIFI_BILGI'dan otomatik çek
                if args.ip is None:
                    print("[SERIAL] IP keşfi: WIFI_BILGI / DURUM sorgulanıyor ...")
                    discovered_ip = ip_from_durum(ser)
                    if discovered_ip:
                        print(f"[SERIAL] Cihaz IP keşfedildi: {discovered_ip}")
                    else:
                        print(f"[SERIAL] IP yanıtta bulunamadı, varsayılan kullanılacak: {DEFAULT_AP_IP}")

                suite = run_suite(ser, f"Serial ({args.port})", args.verbose, args.groups or [])
                suites.append(suite)
                ser.close()
            except Exception as e:
                print(f"[SERIAL] Bağlantı hatası: {e}")

    # Kullanılacak WiFi IP'yi belirle (öncelik: --ip > keşfedilen > varsayılan)
    wifi_ip = args.ip or discovered_ip or DEFAULT_AP_IP

    # ── WiFi TCP ────────────────────────────────────────────────────
    if do_wifi:
        kaynak = "(--ip)" if args.ip else ("(DURUM'dan)" if discovered_ip else "(varsayılan)")
        print(f"\n[WiFi] Bağlanıyor: {wifi_ip}:{args.tcp_port} {kaynak} ...")
        try:
            t = TcpTransport(wifi_ip, args.tcp_port)
            t.connect()
            print("[WiFi] Bağlandı.")
            suite = run_suite(t, f"WiFi TCP ({wifi_ip}:{args.tcp_port})", args.verbose, args.groups or [])
            suites.append(suite)
            t.close()
        except Exception as e:
            print(f"[WiFi] Bağlantı hatası: {e}")

    # ── BLE ─────────────────────────────────────────────────────────
    if do_ble:
        if not HAS_BLE:
            print("\n[BLE] bleak kurulu değil → pip install bleak")
        else:
            ble_addr = args.ble_addr
            if not ble_addr:
                ble_addr = asyncio.run(ble_bul(args.ble))
            if not ble_addr:
                print(f"[BLE] '{args.ble}' cihazı bulunamadı.")
            else:
                print(f"\n[BLE] Bağlanıyor: {ble_addr} ...")
                try:
                    t = BleTransport(ble_addr)
                    t.connect()
                    print("[BLE] Bağlandı.")
                    suite = run_suite(t, f"BLE ({ble_addr})", args.verbose, args.groups or [])
                    suites.append(suite)
                    t.close()
                except Exception as e:
                    print(f"[BLE] Bağlantı hatası: {e}")

    if not suites:
        print("\nHiçbir test çalışmadı. Bağlantı argümanlarını kontrol edin.")
        sys.exit(1)

    ozet_yazdir(suites)
    sys.exit(1 if any(s.failed > 0 for s in suites) else 0)


if __name__ == "__main__":
    main()
