"""
PC araçlarının testleri (Python standart kütüphanesi unittest; ek paket gerekmez)

Çalıştırma (depo kökünden):
    python -m unittest discover tools/tests -v

C tarafıyla ortak dosyalar build_host/ içinden okunur (önce `ctest` çalışmış olmalı):
    build_host/frames_test.bin          (test_frame_codec üretir)
    build_host/gen_signals(.exe), build_host/replay(.exe)
Bulunamazsa ilgili testler "skipped" olarak geçilir. Farklı bir klasör için:
    $env:PD_BUILD_HOST = "C:\\baska\\build_host"
"""

from __future__ import annotations

import io
import os
import random
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))

import decode  # noqa: E402
import event_check  # noqa: E402
import pdproto  # noqa: E402
from pdproto import Frame  # noqa: E402

BUILD = Path(os.environ.get("PD_BUILD_HOST", ROOT / "build_host"))


def exe(name: str) -> Path | None:
    for cand in (BUILD / f"{name}.exe", BUILD / name):
        if cand.is_file():
            return cand
    return None


def frames(n: int, start_seq: int = 0, flags_every: int = 0) -> list[Frame]:
    out = []
    for i in range(n):
        fl = pdproto.FLAG_BUTTON if flags_every and i % flags_every == 0 else 0
        out.append(Frame((start_seq + i) & 0xFF, fl, i, -i, 4096, i * 10, -i * 10, 0))
    return out


def stream(fs: list[Frame]) -> bytes:
    return b"".join(pdproto.pack(f) for f in fs)


# ===========================================================================
# Çerçeve: C (frame_codec) ile birebir aynı byte'lar
# ===========================================================================


class TestFrame(unittest.TestCase):
    def test_exact_bytes_match_c_test(self):
        # test_frame_codec.c / test_exact_byte_layout ile aynı vaka
        f = Frame(seq=7, flags=0x01, ax=0x1234, ay=-1, az=-32768, gx=1, gy=256, gz=0)
        expected = bytes([0xA5, 0x07, 0x01, 0x34, 0x12, 0xFF, 0xFF, 0x00, 0x80,
                          0x01, 0x00, 0x00, 0x01, 0x00, 0x00])
        b = pdproto.pack(f)
        self.assertEqual(b[:15], expected)
        chk = 0
        for x in expected[1:]:
            chk ^= x
        self.assertEqual(b[15], chk)

    def test_roundtrip(self):
        f = Frame(255, 0x1F, 32767, -32768, 0, -1, 1, 4096)
        self.assertEqual(pdproto.unpack(pdproto.pack(f)), f)

    def test_any_single_bit_flip_rejected(self):
        b = bytearray(pdproto.pack(Frame(42, 3, 4096, -4096, 0, 32767, -32768, -1)))
        for i in range(16):
            for bit in range(8):
                b[i] ^= 1 << bit
                self.assertIsNone(pdproto.unpack(bytes(b)), f"byte {i} bit {bit}")
                b[i] ^= 1 << bit
        self.assertIsNotNone(pdproto.unpack(bytes(b)))

    def test_flag_properties(self):
        f = Frame(0, 3 | pdproto.FLAG_BUTTON | pdproto.FLAG_FROZEN, 0, 0, 0, 0, 0, 0)
        self.assertEqual(f.decision, "FIRE")
        self.assertTrue(f.button)
        self.assertTrue(f.frozen)
        self.assertFalse(f.saturated)


# ===========================================================================
# Akış ayrıştırıcı
# ===========================================================================


class TestParser(unittest.TestCase):
    def parse(self, data: bytes, chunk: int | None = None):
        p = pdproto.FrameParser()
        out = []
        if chunk is None:
            out = p.feed(data)
        else:
            for i in range(0, len(data), chunk):
                out += p.feed(data[i:i + chunk])
        p.finish()
        return p, out

    @unittest.skipUnless((BUILD / "frames_test.bin").is_file(), "once ctest calistirin")
    def test_c_generated_file(self):
        """Yol haritası 4.1: C testinin yazdığı .bin doğru çözülmeli."""
        p, out = self.parse((BUILD / "frames_test.bin").read_bytes())
        s = p.stats
        self.assertEqual((s.frames, s.lost, s.checksum_errors, s.skipped_bytes), (300, 0, 0, 0))
        self.assertEqual(s.buttons, 6)  # 0, 50, ..., 250
        for i, (gap, f) in enumerate(out):
            self.assertEqual(gap, 0)
            self.assertEqual(f, Frame(i & 0xFF, pdproto.FLAG_BUTTON if i % 50 == 0 else 0,
                                      i, -i, 4096, i * 10, -i * 10, 0))

    def test_byte_by_byte_same_as_whole(self):
        data = stream(frames(600, flags_every=37))
        _, whole = self.parse(data)
        _, bytewise = self.parse(data, chunk=1)
        _, odd = self.parse(data, chunk=7)
        self.assertEqual(whole, bytewise)
        self.assertEqual(whole, odd)

    def test_seq_wraps_without_loss(self):
        p, _ = self.parse(stream(frames(1000, start_seq=200)))
        self.assertEqual((p.stats.frames, p.stats.lost), (1000, 0))

    def test_deleted_frames_counted_as_lost(self):
        """Yol haritası 4.2: ortadan çerçeve silinince kayıp sayısı doğru çıkmalı."""
        fs = frames(300)
        data = stream(fs[:100] + fs[103:200] + fs[201:])
        p, out = self.parse(data)
        self.assertEqual((p.stats.frames, p.stats.lost), (296, 4))
        gaps = [(g, f.seq) for g, f in out if g]
        self.assertEqual(gaps, [(3, 103), (1, 201)])

    def test_corrupted_byte_is_checksum_error(self):
        """Yol haritası 4.1: bilerek bozulan byte checksum hatası vermeli."""
        data = bytearray(stream(frames(50)))
        data[10 * 16 + 5] ^= 0x40
        p, _ = self.parse(bytes(data))
        self.assertEqual(p.stats.checksum_errors, 1)
        self.assertEqual(p.stats.frames, 49)
        self.assertEqual(p.stats.lost, 1)

    def test_boot_line_before_frames(self):
        data = b"BOOT,0.1.0,POWERON,0x70\n" + stream(frames(10))
        p, _ = self.parse(data)
        self.assertEqual(p.text_lines, ["BOOT,0.1.0,POWERON,0x70"])
        self.assertEqual(p.stats.frames, 10)
        self.assertEqual(p.stats.skipped_bytes, 24)

    def test_sync_byte_inside_data(self):
        # Değerlerin içinde bol 0xA5: ax = 0xA5A5 (-23131), seq = 0xA5
        fs = [Frame(0xA5 + i & 0xFF, 0, -23131, 0xA5, -23131, 0xA5, 0, 0) for i in range(20)]
        p, out = self.parse(stream(fs))
        self.assertEqual(p.stats.frames, 20)
        self.assertEqual([f for _, f in out], fs)

    def test_resync_after_garbage(self):
        rnd = random.Random(5)
        junk = bytes(rnd.randrange(256) for _ in range(37))
        data = stream(frames(20)) + junk + stream(frames(20, start_seq=20))
        p, _ = self.parse(data)
        self.assertEqual(p.stats.frames, 40)
        self.assertEqual(p.stats.lost, 0)

    def test_truncated_last_frame(self):
        data = stream(frames(10))[:-5]
        p, _ = self.parse(data)
        self.assertEqual(p.stats.frames, 9)
        self.assertEqual(p.stats.skipped_bytes, 11)


# ===========================================================================
# Metin satırları (frame_codec.c'deki örneklerle aynı)
# ===========================================================================


class TestLines(unittest.TestCase):
    VALID = [
        ("BOOT,0.1.0,POWERON,0x70\n", "BOOT", {"imu_id": 0x70}),
        ("BOOT,0.1.0,TASK_WDT,NONE", "BOOT", {"imu_id": "NONE"}),
        ("EVT,FIRE,2310,48215,54\n", "EVT", {"gesture": "FIRE", "peak_mg": 2310, "t_ms": 48215}),
        ("EVT,WEB,620,51340,12\r\n", "EVT", {"margin_pct": 12}),
        ("EVT,ICE,1,4294967295,0", "EVT", {"t_ms": 4294967295}),
        ("CFG,MED", "CFG", {"level": "MED"}),
        ("CAL,OK,-34,12,7", "CAL", {"bx": -34}),
        ("CAL,FAIL,-32768,32767,0", "CAL", {"by": 32767}),
        ("ERR,WHOAMI", "ERR", {"fault": "WHOAMI"}),
        ("STAT,612,0,0", "STAT", {"loop_max_us": 612}),
    ]
    INVALID = [
        "EVT,FIRE,2310,48215",          # alan eksik
        "EVT,NONE,1,2,3",               # geçersiz hareket
        "EVT,FIRE,2310,-5,54",          # negatif zaman
        "evt,FIRE,1,2,3",               # küçük harf
        "CFG,MEDIUM",
        "CFG, MED",                     # boşluk
        "BOOT,0.1.0,POWERON,0x7",       # tek hane
        "ERR,TIMEOUT",
        "STAT,1,2",
        "XYZ,1",
        "",
        "EVT,ICE,1,4294967296,0",       # 32 bit sınırı
        "BOOT," + "9" * 60 + ",SW,NONE",  # 64 byte'tan uzun
    ]

    def test_valid_lines(self):
        for text, kind, sub in self.VALID:
            with self.subTest(text=text):
                ln = pdproto.parse_line(text)
                self.assertEqual(ln.type, kind)
                for k, v in sub.items():
                    self.assertEqual(ln.fields[k], v)

    def test_invalid_lines(self):
        for text in self.INVALID:
            with self.subTest(text=text):
                with self.assertRaises(ValueError):
                    pdproto.parse_line(text)


# ===========================================================================
# decode.py
# ===========================================================================


class TestDecode(unittest.TestCase):
    def run_decode(self, data: bytes, fill: bool = True):
        out = io.StringIO()
        r = decode.decode(data, out, fill_gaps=fill)
        rows = out.getvalue().splitlines()
        return r, rows

    def test_header_and_rows(self):
        r, rows = self.run_decode(stream(frames(5)))
        self.assertEqual(rows[0], "seq,flags,ax,ay,az,gx,gy,gz")
        self.assertEqual(rows[1], "0,0,0,0,4096,0,0,0")
        self.assertEqual(rows[5], "4,0,4,-4,4096,40,-40,0")
        self.assertEqual(r.rows, 5)
        self.assertTrue(r.ok)

    def test_gaps_filled_so_row_equals_time(self):
        fs = frames(20)
        r, rows = self.run_decode(stream(fs[:5] + fs[8:]))
        self.assertEqual(r.rows, 20)  # 17 çerçeve + 3 boş satır
        self.assertEqual(rows[6:9], ["5,,,,,,,", "6,,,,,,,", "7,,,,,,,"])
        self.assertEqual(rows[9].split(",")[0], "8")
        self.assertFalse(r.ok)  # 3/20 = %15 kayıp > %0.1

    def test_no_fill(self):
        fs = frames(20)
        r, _ = self.run_decode(stream(fs[:5] + fs[8:]), fill=False)
        self.assertEqual(r.rows, 17)

    def test_lost_pct_limit(self):
        fs = frames(2000)
        r, _ = self.run_decode(stream(fs[:1000] + fs[1001:]))  # 1/2000 = %0.05
        self.assertTrue(r.ok)
        r, _ = self.run_decode(stream(fs[:1000] + fs[1003:]))  # 3/2000 = %0.15
        self.assertFalse(r.ok)

    def test_report_mentions_numbers(self):
        r, _ = self.run_decode(b"BOOT,0.1.0,SW,0x70\n" + stream(frames(10)))
        text = decode.report("x.bin", r)
        self.assertIn("cerceve          : 10", text)
        self.assertIn("BOOT,0.1.0,SW,0x70", text)
        self.assertIn("TAMAM", text)


# ===========================================================================
# event_check.py
# ===========================================================================


class TestEventCheck(unittest.TestCase):
    def test_counts_and_bad_lines(self):
        lines = [
            "BOOT,0.1.0,POWERON,0x70\n",
            "CFG,MED\n",
            "EVT,FIRE,2310,1000,54\n",
            "EVT,FIRE,2400,2000,60\n",
            "EVT,WEB,600,3000,10\n",
            "EVT,ICE,BOZUK\n",
            "\n",
        ]
        r = event_check.check_lines(lines)
        self.assertEqual(r.by_type["EVT"], 3)
        self.assertEqual(r.by_gesture["FIRE"], 2)
        self.assertEqual(len(r.bad), 1)
        self.assertEqual(r.bad[0][0], 6)  # satır numarası
        _, ok = event_check.report(r, {})
        self.assertFalse(ok)

    def test_expectations(self):
        lines = ["EVT,FIRE,2310,1000,54", "EVT,FIRE,2310,2000,54", "CFG,LOW"]
        r = event_check.check_lines(lines)
        self.assertTrue(event_check.report(r, {"FIRE": 2, "CFG": 1})[1])
        self.assertFalse(event_check.report(r, {"FIRE": 3})[1])

    def test_time_backwards_detected_but_reset_allowed(self):
        r = event_check.check_lines(["EVT,WEB,1,5000,0", "EVT,WEB,1,4000,0"])
        self.assertEqual(r.time_backwards, 1)
        r = event_check.check_lines(["EVT,WEB,1,5000,0", "BOOT,0.1.0,TASK_WDT,0x70",
                                     "EVT,WEB,1,100,0"])
        self.assertEqual(r.time_backwards, 0)


# ===========================================================================
# Yol haritası Adım 4 bitti kriteri: C .bin -> decode.py -> CSV -> replay
# ===========================================================================


@unittest.skipUnless(exe("gen_signals") and exe("replay"), "once cmake --build build_host")
class TestChain(unittest.TestCase):
    def test_bin_decode_replay_equals_direct_csv(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            subprocess.run([str(exe("gen_signals")), str(d / "demo.csv"), str(d / "demo.bin")],
                           check=True, capture_output=True)

            # .bin -> CSV (Python)
            rc = decode.main([str(d / "demo.bin"), "-o", str(d / "from_bin.csv")])
            self.assertEqual(rc, 0)

            # C'nin doğrudan yazdığı CSV ile Python'un çözdüğü CSV satır satır aynı
            self.assertEqual((d / "demo.csv").read_text().splitlines(),
                             (d / "from_bin.csv").read_text().splitlines())

            # İkisi de replay'den aynı sonucu verir: WEB, ICE, FIRE
            outs = []
            for name in ("demo.csv", "from_bin.csv"):
                p = subprocess.run([str(exe("replay")), str(d / name)], check=True,
                                   capture_output=True, text=True)
                outs.append(p.stdout.splitlines())
            self.assertEqual(outs[0], outs[1])
            gestures = [ln.split(",")[1] for ln in outs[0][1:]]
            self.assertEqual(gestures, ["WEB", "ICE", "FIRE"])


if __name__ == "__main__":
    unittest.main()
