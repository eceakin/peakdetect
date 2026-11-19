"""
event_check.py - UART metin satırlarını doğrular ve sayar

Kullanım:
    python tools/event_check.py kayit.txt                 # dosyadan
    python tools/event_check.py --port COM5               # canlı, Ctrl+C ile bitir
    python tools/event_check.py --port COM5 --seconds 60
    python tools/event_check.py kayit.txt --expect FIRE=20 --expect CFG=3

Her satır protocol.md'ye göre kontrol edilir. Sonda:
    - satır türlerine göre sayılar (BOOT, EVT, CFG, CAL, ERR, STAT)
    - hareketlere göre sayılar (WEB, ICE, FIRE)
    - bozuk satırlar (satır numarasıyla)
    - EVT zamanlarının ileri gittiği (t_ms azalmıyor; azalırsa araya reset girmiştir)
Çıkış kodu: 0 = hiç bozuk satır yok ve --expect'ler tuttu, 1 = sorun var.
"""

from __future__ import annotations

import argparse
import sys
import time
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pdproto  # noqa: E402


@dataclass
class CheckResult:
    by_type: Counter = field(default_factory=Counter)
    by_gesture: Counter = field(default_factory=Counter)
    bad: list[tuple[int, str, str]] = field(default_factory=list)  # (satır no, metin, sebep)
    time_backwards: int = 0
    _last_t: int | None = None

    def add(self, lineno: int, text: str) -> pdproto.Line | None:
        if not text.strip():
            return None
        try:
            ln = pdproto.parse_line(text)
        except ValueError as e:
            self.bad.append((lineno, text.rstrip("\r\n"), str(e)))
            return None
        self.by_type[ln.type] += 1
        if ln.type == "EVT":
            self.by_gesture[ln.fields["gesture"]] += 1
            t = ln.fields["t_ms"]
            if self._last_t is not None and t < self._last_t:
                self.time_backwards += 1
            self._last_t = t
        elif ln.type == "BOOT":
            self._last_t = None  # yeniden başlatma: zaman sıfırdan başlar
        return ln


def check_lines(lines) -> CheckResult:
    r = CheckResult()
    for i, text in enumerate(lines, start=1):
        r.add(i, text)
    return r


def parse_expect(items: list[str]) -> dict[str, int]:
    out = {}
    for it in items:
        name, _, num = it.partition("=")
        if not num.isdigit():
            raise argparse.ArgumentTypeError(f"--expect bicimi AD=SAYI olmali: {it}")
        out[name.upper()] = int(num)
    return out


def report(r: CheckResult, expect: dict[str, int]) -> tuple[str, bool]:
    ok = not r.bad
    out = ["satir turleri : " + (", ".join(f"{k}={v}" for k, v in sorted(r.by_type.items())) or "-")]
    out.append("hareketler    : " + ", ".join(f"{g}={r.by_gesture.get(g, 0)}" for g in ("WEB", "ICE", "FIRE")))
    out.append(f"bozuk satir   : {len(r.bad)}")
    for lineno, text, why in r.bad[:20]:
        out.append(f"   {lineno}: {text!r}  -> {why}")
    if r.time_backwards:
        out.append(f"UYARI: EVT zamani {r.time_backwards} kez geri gitti (arada reset olmus olabilir)")
    for name, want in expect.items():
        got = r.by_gesture.get(name, r.by_type.get(name, 0))
        mark = "tamam" if got == want else "HATA"
        ok = ok and got == want
        out.append(f"beklenen {name}: {want}, gelen: {got}  [{mark}]")
    out.append("SONUC         : " + ("TAMAM" if ok else "SORUN VAR"))
    return "\n".join(out), ok


def read_serial(port: str, baud: int, seconds: float | None, echo: bool):
    import serial  # pyserial; sadece canlı modda gerekli

    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = port, baud, 0.2
    ser.dtr = False  # DevKit'te DTR/RTS kartı resetleyebilir
    ser.rts = False
    ser.open()
    t_end = time.monotonic() + seconds if seconds else None
    try:
        while t_end is None or time.monotonic() < t_end:
            raw = ser.readline()
            if not raw:
                continue
            text = raw.decode("ascii", errors="replace")
            if echo:
                print(text.rstrip("\r\n"))
            yield text
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="PeakDetect UART satir dogrulayici")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("file", nargs="?", type=Path)
    src.add_argument("--port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float)
    ap.add_argument("--expect", action="append", default=[], help="AD=SAYI, or. FIRE=20")
    ap.add_argument("--quiet", action="store_true", help="canli modda satirlari ekrana yazma")
    args = ap.parse_args(argv)
    expect = parse_expect(args.expect)

    if args.port:
        r = CheckResult()
        for i, text in enumerate(read_serial(args.port, args.baud, args.seconds, not args.quiet), 1):
            r.add(i, text)
    else:
        with open(args.file, encoding="ascii", errors="replace") as fp:
            r = check_lines(fp)

    text, ok = report(r, expect)
    print(text)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
