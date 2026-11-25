# CLAUDE.md — PeakDetect projesi için yapay zekâ bağlamı

Bu dosya, bu depoda çalışan yapay zekâ asistanının (Claude Code, claude.ai Projects vb.)
projeyi baştan anlaması için yazıldı. Bir işe başlamadan önce bu dosyanın tamamını oku.
İnsan ekip üyeleri için kurulum rehberi: `docs/BASLANGIC.md`.

---

## 1. Proje ne?

**CENG424 Embedded System Design** dönem projesi: **PeakDetect – ESP32 Wrist Gesture Controller**.
Ekip: Muhammet Yasin Uysal (lider), Betül Sarı, Ece Akın.

Bileğe takılan bir devre, MPU-6500 IMU ile el hareketini ölçer ve üç hareketi tanır:

| Hareket | Fiziksel tanım | Büyü | Tepki |
|---|---|---|---|
| `WEB` | Bilek silkme (Y ekseninde hızlı dönüş) | Web Shot (örümcek ağı) | Yeşil LED + kısa ses |
| `ICE` | Eli aniden yukarı kaldırma (+Z ivme) | Ice Shield (buz kalkanı) | Mavi LED + yükselen ses |
| `FIRE` | Eli öne sertçe itme (+X ivme) | Fireball (ateş topu) | Kırmızı LED + alçalan ses |

- Aynı hareket iki kez sayılmasın diye 500 ms **cooldown** var.
- **Buton:** kısa basış → hassasiyet LOW → MED → HIGH → LOW; uzun basış (1 s) → kalibrasyon;
  butona basılı açılış → **LOG modu** (ham veri akışı, eşik ayarı için kayıt).
- Ayarlar (hassasiyet + kalibrasyon) NVS'te saklanır, güç kesilince kaybolmaz.
- Opsiyonel (Adım 10): PC'de Wi-Fi/seri köprü ile oyun ekranında efekt. Not buna bağlı değil.
- Orijinal öneri ATmega328P idi; hoca onayıyla **ESP32**'ye geçildi.

## 2. Donanım ve araçlar

- **Kart:** ESP32-WROOM-32 DevKit · **Sensör:** MPU-6500 (I²C, adres 0x68, AD0 = GND)
- **Yazılım:** ESP-IDF **v5.5.6**, C11, Windows
- **PC testleri:** WinLibs gcc + CMake (≥ 3.16) + Unity (CMake FetchContent ile otomatik iner, v2.6.0)
- **PC araçları:** Python ≥ 3.10 (pyserial, numpy, pandas, matplotlib)

Pinler (`firmware/components/drivers/include/board_pins.h` — tek kaynak burası):

| İşlev | GPIO | Not |
|---|---|---|
| I²C SDA / SCL | 21 / 22 | 400 kHz |
| LED WEB / ICE / FIRE | 25 / 26 / 27 | yeşil 220 Ω / mavi 100 Ω / kırmızı 220 Ω, aktif yüksek |
| Hata LED'i | 2 | kart üstü LED |
| Buzzer | 18 | LEDC PWM, 100 Ω seri |
| Buton | 4 | aktif düşük, dahili pull-up |
| Debug: tick / ARMED | 23 / 19 | mantık analizörü için |
| UART | UART0 (USB köprüsü) | 115200 8N1 |

Eksen kuralı (sensör elin sırtında): **X** parmaklara doğru (ileri), **Y** yanal (silkme ekseni),
**Z** elin sırtından yukarı.

## 3. Depo yapısı ve katmanlar

```
firmware/                     ESP-IDF projesi (idf.py burada çalışır)
  main/                       app_main (şimdilik sadece "PeakDetect boot"), Kconfig.projbuild
  components/core/            SAF C. ESP-IDF başlığı YOK, PC'de de derlenir
    include/pd_types.h        ortak tipler (bütün modüller buna dayanır)
    include/app_config.h      BÜTÜN sayılar (süre, eşik, limit)
  components/drivers/         donanıma dokunan tek katman (board_pins.h burada) — şimdilik iskelet
  components/services/        sürücü + core'u kullanıcı davranışına çeviren katman — şimdilik iskelet
  components/app/             app_main, mod durum makinesi, sampling_task — şimdilik iskelet
host_tests/                   Unity testleri + signals.c (yapay sinyal) + replay.c (kayıt oynatıcı)
tools/                        Python: pdproto, decode, record, event_check (+ tools/tests)
docs/protocol.md              UART protokolü (metin satırları + 16 byte çerçeve), byte byte
docs/test_records/            kartlı testlerin kanıtları (tarihli)
data/                         CSV kayıtlar (ham .bin'ler data/raw/ altında, git'e girmez)
bringup/                      Adım 5'te her sürücü için tek başına deneme programları
```

Bağımlılık yönü (ihlal edilirse PC testleri derlenmez):

```
app ──► services ──► drivers
  │         │
  └────────►└──────► core   (core hiçbir şeye bağlı değil, ESP-IDF'e de)
```

## 4. Bitmiş core modülleri (DOKUNMA, kullanabilirsin)

Hepsi PC'de Unity testleriyle doğrulandı. Değişiklik gerekiyorsa önce ekibe sor.

| Modül | Ne yapar | Ana API |
|---|---|---|
| `crc16` | CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF; "123456789" → 0x29B1). settings_t doğrulaması için | `crc16()`, `crc16_update()` |
| `frame_codec` | 16 byte log çerçevesi paketle/çöz; BOOT/EVT/CFG/CAL/ERR/STAT metin satırları | `frame_pack/unpack/from_raw`, `line_boot/evt/cfg/cal/err/stat`, `gesture_name()` … |
| `imu_convert` | ham LSB → mg ve 0.1 dps; bias, Q10 ölçek, doyma bayrağı | `imu_convert()`, `calib_set_default()`, `calib_is_valid()` |
| `sensitivity` | Medium eşik tablosu; LOW ×1.30, HIGH ×0.75 (yalnızca alt eşikler; `web_a_max` ve `dominance_x10` ölçeklenmez) | `sensitivity_apply()`, `sensitivity_next()`, `sensitivity_is_valid()` |
| `gravity_filter` | yerçekimi tahmini (içeride ×64), doğrusal ivme, dondurma | `gravity_reset()`, `gravity_update()`, `gravity_get()`, `isqrt_u32()` |
| `gesture_classifier` | 60 ms pencerenin tepelerinden karar; margin ve ret sebebi. Durumsuz | `classifier_classify()`, `classifier_rule_passes()` |
| `gesture_detector` | IDLE → WINDOW → COOLDOWN → REARM durum makinesi | `detector_init()`, `detector_update()`, `detector_wants_freeze()` |

Sınıflandırma kuralları (eşikler `sensitivity_apply` ile gelir):

```
WEB  : w_abs[Y] >= web_w_min   VE  a_mag   <  web_a_max      (dönüş baskın)
ICE  : a_pos[Z] >= ice_a_min   VE  a_pos[Z] > 1.5 * a_abs[X] (yukarı baskın)
FIRE : a_pos[X] >= fire_a_min  VE  a_pos[X] > 1.5 * a_abs[Z] (ileri baskın)
Karar sırası: doymuş → SATURATED; kural yok → NO_RULE / WEB_TOO_HARD;
iki kural → margin farkı >= %20 ise büyüğü, değilse AMBIGUOUS.
```

Medium başlangıç eşikleri (tahmin; Adım 8'de gerçek veriyle güncellenecek):
tetik 800 mg / 250 °/s, WEB ≥ 350 °/s ve |a| < 1.5 g, ICE ≥ 1.2 g, FIRE ≥ 1.5 g, baskınlık 1.5.

Her örnekteki (5 ms) core zinciri — kartta da replay'de de **aynı sıra**:

```c
imu_convert(&raw, &calib, &sample);
gravity_update(&sample, detector_wants_freeze(), &motion);
if (detector_update(&motion, sample.saturated, now, &result)) {
    /* result.gesture != GESTURE_NONE ise kabul -> LED/ses + EVT satırı */
}
```

Yardımcılar (yalnız PC, `host_tests/`): `signals.c` (yapay IMU: hareketsiz el, WEB/ICE/FIRE,
normal kol hareketi; çıktısı `imu_raw_t`), `replay.c` + `replay` programı (CSV → zincir → olaylar),
`gen_signals` (12 s demo CSV/BIN: 1 s WEB, 3 s ICE, 5 s FIRE, 7–12 s normal hareket).

## 5. Protokol özeti (ayrıntı: `docs/protocol.md`)

- **Metin satırları** (ASCII, virgülle, tek `\n`, en fazla 64 byte):
  `BOOT,<fw>,<reset>,<imu_id>` · `EVT,<gesture>,<peak_mg>,<t_ms>,<margin_pct>` · `CFG,<LOW|MED|HIGH>` ·
  `CAL,<OK|FAIL>,<bx>,<by>,<bz>` · `ERR,<I2C|WHOAMI|STUCK|OVERRUN|NVS>` · `STAT,<loop_max_us>,<overruns>,<i2c_errors>` (yalnız debug)
- **LOG çerçevesi** (16 byte, little-endian): `0xA5 | seq | flags | ax ay az gx gy gz (int16, ham LSB) | XOR(1..14)`.
  flags: bit1:0 karar (gesture_t), bit2 buton işareti, bit3 doymuş, bit4 yerçekimi donuk.
- **Değişiklik kuralı:** protokol değişirse **aynı commit'te** `docs/protocol.md` + `frame_codec.c` + `tools/pdproto.py` güncellenir ve testler geçer.

## 6. Kodlama kuralları (şimdiye kadar hep böyle yaptık — UY)

1. **Saf mantık `core/` içine** yazılır (PC testleri yalnızca core'u derler). Donanıma bağlayan ince
   katman `services/` içine kart gelince yazılacak: şimdilik onun yalnızca `.h` arayüzünü taslak olarak ver.
2. **Core ESP-IDF'e dokunmaz.** Zaman parametre olarak gelir: `pd_tick_t now` (1 tick = 5 ms, 200 Hz döngü).
   Tick karşılaştırmaları taşmaya dayanıklı yapılır: `(int32_t)(now - deadline) < 0`.
3. Modül durumu `.c` içinde **`static`** tutulur; her modülün bir `xxx_reset()` ya da `xxx_init()`
   fonksiyonu olur (testler birbirini etkilemesin; `setUp()` içinde çağrılır).
4. **Kodda sabit sayı yazılmaz.** Yeni sabitler `app_config.h`'e `PD_` önekiyle, birim yorumuyla eklenir;
   süreler 5 ms'nin katıdır (ms ve tick karşılığı birlikte). Pinler yalnızca `board_pins.h`'te.
5. **Tamsayı aritmetiği**; önce çarp sonra böl (`raw * 125 / 512`); taşma riskinde `int64_t` ara sonuç;
   `int32_t` yazdırırken `PRId32`/`PRIu32`; negatif sayıyı `<<` ile kaydırma.
6. **Yorumlar Türkçe**; struct alanlarının yanında birim yazılır (`mg`, `0.1 dps`, `tick`, `LSB`).
7. `-Wall -Wextra` ile **uyarısız** derlenmeli (teslimden önce `-Werror` ile de denenir).
8. Testler: `host_tests/test_<modul>.c`, Unity. `host_tests/CMakeLists.txt`'ye eklenecek satır:
   `pd_add_test(test_<modul>)` — yapay IMU sinyali gerekiyorsa `pd_add_test(test_<modul> signals)`.
9. `pd_types.h` ve `app_config.h` ortak sözleşmedir: değiştirmeden önce ekibe haber ver.

## 7. Derleme ve test komutları (Windows)

İki terminali **karıştırma**:

| Komut | Terminal |
|---|---|
| `idf.py ...` | VS Code → **ESP-IDF: Open ESP-IDF Terminal** |
| `cmake`, `ctest`, `gcc`, `git`, `python` | Normal PowerShell |

```powershell
# PC testleri (depo kökünde, normal PowerShell)
cmake -S host_tests -B build_host -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc   # ilk sefer
cmake --build build_host
ctest --test-dir build_host --output-on-failure        # beklenen: 100% tests passed

# Python araç testleri (ctest'ten sonra; build_host'taki dosyaları kullanır)
python -m unittest discover tools/tests -v

# Firmware (ESP-IDF terminali)
cd firmware
idf.py build                                            # beklenen: Project build complete
idf.py -p COM5 flash monitor                            # kart varsa; çıkış Ctrl + ]
```

Tek dosyayı hızlı kontrol (yapay zekâ teslimden önce böyle derler):
`gcc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -I firmware/components/core/include ...`

## 8. Yol haritası ve şu anki durum

Ayrıntılı plan: "PeakDetect – Yazılım Yol Haritası (Adım Adım)" belgesi (12 adım, 0–11).

| Adım | Konu | Durum |
|---|---|---|
| 0 | Ortam doğrulama | ✅ (kartla ilgili madde kart gelince) |
| 1 | Git deposu + proje iskeleti | ✅ |
| 2 | Ortak sözleşmeler: `pd_types.h`, `app_config.h`, `board_pins.h`, `docs/protocol.md` | ✅ |
| 3 | 7 core modülü + `signals` + `replay` | ✅ tüm testler geçiyor |
| 4 | Python araçları: `pdproto`, `decode`, `event_check`, `record` (+ testler) | ✅ (`analyze.py` Adım 8'e bırakıldı) |
| 5 | Kart bring-up ve sürücüler (blink, led_reg, uart_boot, i2c_scan, imu_raw, tick_reg, button_reg, tones, wdt_test, stream) | ⏳ kart gelince |
| 6 | Servisler: button_handler, feedback_player, settings_store, calibration, event_reporter, raw_logger, fault_monitor, loop_stats | 🔜 **sıradaki iş** (saf kısımları kart olmadan yazılabilir) |
| 7 | Entegrasyon (app_main, app_controller, sampling_task) → `git tag v0.1-first-e2e` | — |
| 8 | Veri toplama, `analyze.py`, eşik ayarı | — |
| 9 | Sistem testleri ve kanıtlar → `git tag v1.0` | — |
| 10 | Opsiyonel Wi-Fi / oyun bağlantısı | — |
| 11 | Teslim hazırlığı | — |

**Servis yazma kalıbı (Adım 6):** her servis iki parça — (a) girdi alıp karar veren **saf fonksiyon**
`core/` içinde, PC'de test edilir (ör. "buton seviyesi + zaman → olay"); (b) bunu sürücüye bağlayan
**ince katman** `services/` içinde, kart gelince yazılır (şimdilik yalnızca `.h` taslağı).

Adım 6 servislerinin PC test hedefleri:
- `button_handler`: zıplayan sinyalden tek SHORT; 1 s basılı → tek LONG; açılışta basılı → HELD_AT_BOOT
  (`PD_DEBOUNCE_SAMPLES` = 6 örnek = 30 ms, `PD_LONG_PRESS_TICKS` = 200)
- `feedback_player`: sahte zamanla adımların sırası/süresi; yeni desenin eskisini kesmesi;
  öncelik FAULT > CAL_* > ability > SENS_* > BOOT (`pattern_id_t`)
- `settings_store`: bozuk CRC, yanlış sürüm, aralık dışı değer → varsayılana dön
- `calibration`: yapay hareketsiz sinyalde doğru bias (200 örnek); hareketli sinyalde FAIL
- `event_reporter`: her olay türü için doğru satır; LOG modunda susma
- `raw_logger`: seq 255 → 0 dönmesi, bayrak bitleri
- `fault_monitor`: 3 hatada kurtarma isteği; kurtarma da başarısızsa FAULT; 200 aynı örnekte STUCK

## 9. Yapay zekâdan beklenen teslim biçimi

- `.h`, `.c` ve test dosyasını **dosya olarak** ver; `app_config.h`'e eklenecek satırları ve
  `CMakeLists.txt` satırını **ayrıca** yaz.
- Vermeden önce kodu **gerçekten derle ve testleri çalıştır**
  (`gcc -std=c11 -Wall -Wextra -Werror`, mümkünse `-fsanitize=undefined`); sonucu söyle.
- Sonunda kısa **Türkçe** açıklama: modül ne yapıyor, hangi tasarım kararları verildi, testler neyi kanıtlıyor.
- Proje önerisinde net olmayan bir karar verdiysen **ayrıca listele** — ekipçe onaylanacak.
- Bitmiş core modüllerini, `pd_types.h`'i ve protokolü sessizce değiştirme; gerekiyorsa öner ve gerekçesini yaz.

## 10. Git çalışma düzeni

- `main` her zaman derlenir ve bütün testleri geçer. Her modül/servis için ayrı dal:
  `git checkout -b service/button_handler`.
- Küçük commit'ler, ne yapıldığını söyleyen mesajlar: `core: gravity_filter + 6 test`,
  `services: button_handler saf mantik + 9 test`.
- Dal bitince GitHub'da Pull Request açılır, ekipten biri bakıp `main`'e birleştirir.
- Kartlı her adımın kanıtı (seri port çıktısı, fotoğraf, analizör ekranı) tarihle `docs/test_records/`'a.
- **30 dakika kuralı:** bir sorunla yarım saatten fazla uğraşınca dur; sorunu, denenenleri ve hata
  çıktısını yazıp ekibe ya da Claude'a sor.

## 11. Sık karşılaşılan sorunlar

| Belirti | Çözüm |
|---|---|
| ESP-IDF derlemesi garip yol hataları veriyor | Depo boşluk/Türkçe karakter içermeyen yolda olmalı: `C:\esp\peakdetect` |
| `CMakeLists.txt not found in project directory` | `idf.py`'ı `firmware/` içinde çalıştır |
| CMake derleyici olarak `esp-clang`/xtensa seçiyor | PC testlerini normal PowerShell'de çalıştır; `build_host`'u silip `-DCMAKE_C_COMPILER=gcc` ile tekrar |
| `where.exe gcc` önce `C:\MinGW\bin` gösteriyor | Eski MinGW'yi PATH'ten kaldır ya da WinLibs'in altına taşı |
| "does not match the generator used previously" | `build_host` klasörünü sil |
| Yükleme `Connecting...`'te takılıyor | Kartın **BOOT** tuşunu basılı tut |
| `record.py` açılınca kart resetleniyor | DTR/RTS kapalı açılır (kodda var); LOG modu için kartı butona basılı yeniden aç |
