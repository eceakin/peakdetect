"""
pdproto.py - PeakDetect UART protokolünün Python tarafı (docs/protocol.md ile birebir)

Diğer bütün araçlar (decode, record, event_check, analyze) bu modülü kullanır.
Protokol değişirse aynı commit'te: docs/protocol.md + frame_codec.c + bu dosya.

1) 16 byte'lık ham veri çerçevesi (LOG modu):
     0     sync 0xA5
     1     seq (0..255)
     2     flags
     3..14 ax, ay, az, gx, gy, gz  (int16, little-endian, ham LSB)
     15    byte 1..14'ün XOR'u

2) ASCII metin satırları: BOOT, EVT, CFG, CAL, ERR, STAT
"""

from __future__ import annotations

import re
import struct
from dataclasses import dataclass, field

SYNC = 0xA5
FRAME_LEN = 16
TICK_MS = 5

FLAG_DECISION_MASK = 0x03
FLAG_BUTTON = 0x04
FLAG_SATURATED = 0x08
FLAG_FROZEN = 0x10

GESTURES = {0: "NONE", 1: "WEB", 2: "ICE", 3: "FIRE"}

_BODY = struct.Struct("<BB6h")  # seq, flags, ax, ay, az, gx, gy, gz (byte 1..14)

CSV_HEADER = ["seq", "flags", "ax", "ay", "az", "gx", "gy", "gz"]


# ---------------------------------------------------------------------------
# 1) Çerçeve
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class Frame:
    seq: int
    flags: int
    ax: int
    ay: int
    az: int
    gx: int
    gy: int
    gz: int

    @property
    def decision(self) -> str:
        return GESTURES[self.flags & FLAG_DECISION_MASK]

    @property
    def button(self) -> bool:
        return bool(self.flags & FLAG_BUTTON)

    @property
    def saturated(self) -> bool:
        return bool(self.flags & FLAG_SATURATED)

    @property
    def frozen(self) -> bool:
        return bool(self.flags & FLAG_FROZEN)

    def as_row(self) -> list[int]:
        return [self.seq, self.flags, self.ax, self.ay, self.az, self.gx, self.gy, self.gz]


def checksum(buf: bytes) -> int:
    """Byte 1..14'ün XOR'u."""
    x = 0
    for b in buf[1:15]:
        x ^= b
    return x


def pack(f: Frame) -> bytes:
    """Frame -> 16 byte (testler ve yapay veri için; ayrılmış flag bitleri sıfırlanır)."""
    body = _BODY.pack(f.seq & 0xFF, f.flags & 0x1F, f.ax, f.ay, f.az, f.gx, f.gy, f.gz)
    buf = bytes([SYNC]) + body
    return buf + bytes([checksum(buf + b"\x00")])


def unpack(buf: bytes) -> Frame | None:
    """16 byte -> Frame. Sync ya da checksum tutmazsa None."""
    if len(buf) != FRAME_LEN or buf[0] != SYNC or buf[15] != checksum(buf):
        return None
    return Frame(*_BODY.unpack(buf[1:15]))


@dataclass
class ParserStats:
    frames: int = 0           # sağlam çerçeve
    checksum_errors: int = 0  # çerçeve beklenen yerde bozuk çerçeve
    lost: int = 0             # seq atlamasından anlaşılan kayıp çerçeve
    skipped_bytes: int = 0    # çerçeve dışı byte (BOOT satırı, eşleşme kaybı)
    buttons: int = 0          # buton işaretli çerçeve


class FrameParser:
    """
    Akıştan çerçeve ayıklar. Veri parça parça gelebilir (seri port), feed() her
    çağrıda o ana kadar tamamlanan çerçeveleri döndürür.

    Kurallar (protocol.md, "Alıcı kuralları"):
      1. 0xA5 aranır, 16 byte alınır, checksum kontrol edilir.
      2. Tutmazsa bir byte ilerlenip yeniden aranır (veri içinde tesadüfi 0xA5).
      3. seq +1 değilse aradaki fark kadar çerçeve kayıp sayılır (255 -> 0 normal).
      4. Çerçeveden önceki metin (LOG modundaki BOOT satırı) text_lines'a toplanır.

    Kayıp çerçeveler için callback yerine `gaps` listesi tutulur:
    (kayıp_öncesi_seq, kayıp_sayısı) — decode.py boşlukları doldurmak için kullanır.
    """

    def __init__(self) -> None:
        self.stats = ParserStats()
        self.text_lines: list[str] = []
        self._buf = bytearray()
        self._text = bytearray()
        self._locked = False
        self._prev_seq: int | None = None

    def feed(self, data: bytes) -> list[tuple[int, Frame]]:
        """
        Yeni byte'ları işler. Dönüş: [(kayıp_sayısı, Frame), ...]
        kayıp_sayısı, bu çerçeveden hemen önce kaybolan çerçeve sayısıdır (çoğunlukla 0).
        """
        self._buf += data
        out: list[tuple[int, Frame]] = []
        buf = self._buf
        while len(buf) > 0:
            if buf[0] != SYNC:
                if self._locked:
                    self._locked = False  # eşleşme kayboldu
                self._skip_byte()
                continue
            if len(buf) < FRAME_LEN:
                break  # devamı gelsin
            f = unpack(bytes(buf[:FRAME_LEN]))
            if f is None:
                if self._locked:
                    self.stats.checksum_errors += 1
                    self._locked = False
                self._skip_byte()
                continue

            gap = 0
            if self._prev_seq is not None:
                gap = (f.seq - self._prev_seq - 1) & 0xFF
            self.stats.lost += gap
            self._prev_seq = f.seq
            self.stats.frames += 1
            if f.button:
                self.stats.buttons += 1
            self._locked = True
            self._flush_text()
            del buf[:FRAME_LEN]
            out.append((gap, f))
        return out

    def finish(self) -> None:
        """Akış bitti: kalan byte'lar çerçeve değildir."""
        self.stats.skipped_bytes += len(self._buf)
        self._text += self._buf
        self._buf.clear()
        self._flush_text()

    def _skip_byte(self) -> None:
        self._text.append(self._buf[0])
        del self._buf[0]
        self.stats.skipped_bytes += 1
        if self._text.endswith(b"\n"):
            self._flush_text()
        elif len(self._text) > 256:  # satır sonu gelmeyen ikili çöp: biriktirme
            self._text.clear()

    def _flush_text(self) -> None:
        for raw in bytes(self._text).split(b"\n"):
            s = raw.decode("ascii", errors="replace").strip()
            if s and _LINE_TYPE.match(s):
                self.text_lines.append(s)
        self._text.clear()


# ---------------------------------------------------------------------------
# 2) Metin satırları
# ---------------------------------------------------------------------------

_LINE_TYPE = re.compile(r"^(BOOT|EVT|CFG|CAL|ERR|STAT),")

_INT = r"-?\d+"
_UINT = r"\d+"
_PATTERNS = {
    "BOOT": re.compile(
        r"^BOOT,(?P<fw>[0-9A-Za-z._-]+),"
        r"(?P<reset>POWERON|EXT|SW|PANIC|INT_WDT|TASK_WDT|WDT|BROWNOUT|OTHER),"
        r"(?P<imu_id>0x[0-9A-F]{2}|NONE)$"
    ),
    "EVT": re.compile(
        rf"^EVT,(?P<gesture>WEB|ICE|FIRE),(?P<peak_mg>{_INT}),(?P<t_ms>{_UINT}),"
        rf"(?P<margin_pct>{_INT})$"
    ),
    "CFG": re.compile(r"^CFG,(?P<level>LOW|MED|HIGH)$"),
    "CAL": re.compile(rf"^CAL,(?P<result>OK|FAIL),(?P<bx>{_INT}),(?P<by>{_INT}),(?P<bz>{_INT})$"),
    "ERR": re.compile(r"^ERR,(?P<fault>I2C|WHOAMI|STUCK|OVERRUN|NVS)$"),
    "STAT": re.compile(
        rf"^STAT,(?P<loop_max_us>{_UINT}),(?P<overruns>{_UINT}),(?P<i2c_errors>{_UINT})$"
    ),
}
_NUMERIC = {"peak_mg", "t_ms", "margin_pct", "bx", "by", "bz", "loop_max_us", "overruns",
            "i2c_errors"}

LINE_MAX = 64  # '\n' dahil


@dataclass
class Line:
    type: str
    fields: dict = field(default_factory=dict)
    raw: str = ""


def parse_line(text: str) -> Line:
    """
    Tek bir metin satırını doğrular ve ayrıştırır. Sondaki '\\n' / '\\r\\n' kabul edilir.
    Biçim protocol.md'ye uymuyorsa ValueError.
    """
    s = text.rstrip("\r\n")
    if len(s) + 1 > LINE_MAX:
        raise ValueError(f"satır {LINE_MAX} byte'tan uzun")
    if "\r" in s or "\n" in s or " " in s:
        raise ValueError("satırda boşluk ya da satır sonu karakteri var")
    kind = s.split(",", 1)[0]
    pat = _PATTERNS.get(kind)
    if pat is None:
        raise ValueError(f"bilinmeyen satır türü: {kind!r}")
    m = pat.match(s)
    if m is None:
        raise ValueError(f"{kind} satırı biçime uymuyor: {s!r}")
    fields = {k: (int(v) if k in _NUMERIC else v) for k, v in m.groupdict().items()}
    if kind == "BOOT" and fields["imu_id"] != "NONE":
        fields["imu_id"] = int(fields["imu_id"], 16)
    if kind == "STAT" or kind == "EVT":
        for k in ("t_ms", "loop_max_us", "overruns", "i2c_errors"):
            if k in fields and fields[k] > 0xFFFFFFFF:
                raise ValueError(f"{k} 32 bit sınırını aşıyor")
    return Line(kind, fields, s)
