"""
decode.py - ham log kaydı (.bin) -> CSV

Kullanım:
    python tools/decode.py data/raw/ece_fire_01.bin
    python tools/decode.py data/raw/ece_fire_01.bin -o data/ece_fire_01.csv
    python tools/decode.py kayit.bin --no-fill

CSV sütunları (replay ve analyze.py ile aynı):
    seq,flags,ax,ay,az,gx,gy,gz

Kayıp çerçeveler: varsayılan olarak her kayıp çerçeve için değerleri boş bir satır
yazılır (ör. "17,,,,,,,"). Böylece satır numarası = zaman (5 ms) kuralı bozulmaz;
replay bu satırları "hatalı satır" sayar ama zamanı ilerletir. --no-fill ile kapatılır.

Sonda bir rapor yazar: toplam çerçeve, checksum hatası, kayıp, süre, buton işaretleri.
Çıkış kodu: 0 = kayıp oranı < %0.1 ve checksum hatası yok, 1 = kayıt şüpheli, 2 = kullanım hatası.
"""

from __future__ import annotations

import argparse
import csv
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pdproto  # noqa: E402

LOST_LIMIT_PCT = 0.1  # proje önerisi Bölüm 16: <= %0.1 kayıp


@dataclass
class DecodeResult:
    rows: int
    stats: pdproto.ParserStats
    text_lines: list[str]

    @property
    def lost_pct(self) -> float:
        total = self.stats.frames + self.stats.lost
        return 100.0 * self.stats.lost / total if total else 0.0

    @property
    def ok(self) -> bool:
        return self.stats.checksum_errors == 0 and self.lost_pct <= LOST_LIMIT_PCT


def decode(data: bytes, out, fill_gaps: bool = True) -> DecodeResult:
    """data (bin içeriği) -> out (metin akışı) CSV. Sonuç istatistiklerini döndürür."""
    parser = pdproto.FrameParser()
    w = csv.writer(out, lineterminator="\n")
    w.writerow(pdproto.CSV_HEADER)
    rows = 0
    for gap, f in parser.feed(data):
        if fill_gaps:
            for k in range(gap, 0, -1):
                w.writerow([(f.seq - k) & 0xFF] + [""] * 7)
                rows += 1
        w.writerow(f.as_row())
        rows += 1
    parser.finish()
    return DecodeResult(rows, parser.stats, parser.text_lines)


def report(name: str, r: DecodeResult) -> str:
    s = r.stats
    dur = (s.frames + s.lost) * pdproto.TICK_MS / 1000.0
    lines = [
        f"dosya            : {name}",
        f"cerceve          : {s.frames}",
        f"sure             : {dur:.1f} s",
        f"checksum hatasi  : {s.checksum_errors}",
        f"kayip cerceve    : {s.lost} (%{r.lost_pct:.3f})",
        f"atlanan byte     : {s.skipped_bytes}",
        f"buton isareti    : {s.buttons}",
    ]
    for t in r.text_lines:
        lines.append(f"metin satiri     : {t}")
    lines.append("SONUC            : " + ("TAMAM" if r.ok else "SUPHELI - kaydi tekrarlayin"))
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="PeakDetect .bin -> .csv")
    ap.add_argument("input", type=Path)
    ap.add_argument("-o", "--output", type=Path, help="varsayilan: girdi adi, .csv uzantili")
    ap.add_argument("--no-fill", action="store_true", help="kayip cerceveler icin bos satir yazma")
    args = ap.parse_args(argv)

    if not args.input.is_file():
        print(f"dosya yok: {args.input}", file=sys.stderr)
        return 2
    out_path = args.output or args.input.with_suffix(".csv")

    data = args.input.read_bytes()
    with open(out_path, "w", newline="", encoding="ascii") as fp:
        r = decode(data, fp, fill_gaps=not args.no_fill)

    print(report(str(args.input), r))
    print(f"yazildi          : {out_path} ({r.rows} satir)")
    return 0 if r.ok else 1


if __name__ == "__main__":
    sys.exit(main())
