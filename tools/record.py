"""
record.py - seri porttan ham log çerçevelerini .bin dosyasına kaydeder

Kullanım (kart LOG modunda, yani butona basılı açılmış olmalı):
    python tools/record.py --port COM5 -o data/raw/ece_fire_01.bin
    python tools/record.py --port COM5 -o data/raw/ece_normal_01.bin --seconds 120

Kayıt Ctrl+C ile ya da --seconds dolunca biter. Dosyaya port'tan gelen byte'lar
DEĞİŞTİRİLMEDEN yazılır (ayrıştırma decode.py'nin işi); ekranda her saniye
çerçeve/s, kayıp ve buton işareti sayısı gösterilir. Hedef: 200 çerçeve/s.

Bitince otomatik olarak decode.py raporu çalıştırılır (--no-decode ile kapatılır).
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pdproto  # noqa: E402


class LiveMonitor:
    """Gelen byte'ları ayrıştırıp saniyelik özet üretir (dosyaya yazılanı etkilemez)."""

    def __init__(self) -> None:
        self.parser = pdproto.FrameParser()
        self._last_frames = 0
        self._last_t = time.monotonic()

    def feed(self, data: bytes) -> None:
        self.parser.feed(data)

    def line(self, elapsed: float) -> str:
        s = self.parser.stats
        now = time.monotonic()
        rate = (s.frames - self._last_frames) / max(now - self._last_t, 1e-6)
        self._last_frames, self._last_t = s.frames, now
        return (f"{elapsed:6.1f} s | {rate:5.0f} cerceve/s | toplam {s.frames:6d} | "
                f"kayip {s.lost:4d} | checksum {s.checksum_errors:3d} | buton {s.buttons:3d}")


def record(port: str, baud: int, out: Path, seconds: float | None) -> int:
    import serial  # pyserial

    out.parent.mkdir(parents=True, exist_ok=True)
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = port, baud, 0.05
    ser.dtr = False  # DevKit'te DTR/RTS kartı resetleyebilir: LOG modu kaybolur
    ser.rts = False
    ser.open()

    mon = LiveMonitor()
    total = 0
    t0 = time.monotonic()
    next_print = t0 + 1.0
    print(f"kayit basladi: {out}  (durdurmak icin Ctrl+C)")
    try:
        with open(out, "wb") as fp:
            while True:
                now = time.monotonic()
                if seconds is not None and now - t0 >= seconds:
                    break
                data = ser.read(4096)
                if data:
                    fp.write(data)
                    total += len(data)
                    mon.feed(data)
                if now >= next_print:
                    print(mon.line(now - t0))
                    next_print += 1.0
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()

    print(f"bitti: {total} byte, {mon.parser.stats.frames} cerceve -> {out}")
    if mon.parser.stats.frames == 0:
        print("UYARI: hic cerceve gelmedi. Kart LOG modunda mi (butona basili acilis)? Port dogru mu?")
    return total


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="PeakDetect ham veri kaydedici")
    ap.add_argument("--port", required=True, help="or. COM5")
    ap.add_argument("-o", "--output", type=Path, required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float)
    ap.add_argument("--no-decode", action="store_true")
    args = ap.parse_args(argv)

    if args.output.exists():
        print(f"{args.output} zaten var; uzerine yazilmasin diye durdum.", file=sys.stderr)
        return 2

    record(args.port, args.baud, args.output, args.seconds)

    if not args.no_decode:
        import decode
        return decode.main([str(args.output), "-o", str(csv_path_for(args.output))])
    return 0


def csv_path_for(bin_path: Path) -> Path:
    """data/raw/x.bin -> data/x.csv  (ham kayıt depoya girmez, CSV girer); diğerleri yanına."""
    if bin_path.parent.name == "raw":
        return bin_path.parent.parent / (bin_path.stem + ".csv")
    return bin_path.with_suffix(".csv")


if __name__ == "__main__":
    sys.exit(main())
