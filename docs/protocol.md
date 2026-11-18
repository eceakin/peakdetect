# PeakDetect – UART Protokolü

Sürüm: 1 · Firmware ile birebir aynı tutulur (Adım 11 kontrol listesi).

Bağlantı: UART0, DevKit'in USB köprüsü üzerinden, **115200 baud, 8N1**, akış kontrolü yok.

Port iki moddan birinde çalışır:

| Mod | Ne gönderilir |
|---|---|
| Normal (RUN, CALIBRATE, FAULT) | Yalnızca ASCII metin satırları (Bölüm 1) |
| LOG (butona basılı açılış) | Önce tek bir `BOOT` satırı, sonra yalnızca 16 byte'lık ikili çerçeveler (Bölüm 2) |

Demo derlemesinde ESP-IDF'in kendi açılış ve log mesajları kapatılır; port yalnızca bu belgede tanımlanan veriyi taşır.

---

## 1. Metin satırları

### Genel kurallar

- Yalnızca ASCII. Alanlar virgülle ayrılır, boşluk yoktur.
- Her satır tek `\n` ile biter (`\r` yok). Satır uzunluğu `\n` dahil en fazla **64 byte**.
- İlk alan satır türüdür (büyük harf). Sayılar ondalık tamsayıdır, gerekiyorsa başında `-` olur.
- Tanınmayan bir satır türü görülürse alıcı satırı atlar (ileride yeni tür eklenebilir).

### Satır türleri

| Tür | Biçim | Ne zaman |
|---|---|---|
| BOOT | `BOOT,<fw>,<reset>,<imu_id>` | Açılışta bir kez |
| EVT | `EVT,<gesture>,<peak_mg>,<t_ms>,<margin_pct>` | Kabul edilen her harekette |
| CFG | `CFG,<level>` | Hassasiyet değişince |
| CAL | `CAL,<result>,<bx>,<by>,<bz>` | Kalibrasyon bitince |
| ERR | `ERR,<fault>` | Hata algılanınca |
| STAT | `STAT,<loop_max_us>,<overruns>,<i2c_errors>` | Saniyede bir, **yalnızca debug derlemesinde** |

### Alanlar

| Alan | Değerler / birim |
|---|---|
| `fw` | Firmware sürümü, `PD_FW_VERSION` (ör. `0.1.0`) |
| `reset` | `POWERON`, `EXT`, `SW`, `PANIC`, `INT_WDT`, `TASK_WDT`, `WDT`, `BROWNOUT`, `OTHER` |
| `imu_id` | WHO_AM_I değeri, onaltılık: `0x70`. Okunamadıysa `NONE` |
| `gesture` | `WEB`, `ICE`, `FIRE` |
| `peak_mg` | Penceredeki en büyük \|a_lin\| (vektör), mg |
| `t_ms` | Açılıştan beri ms (tick × 5). 32 bit, ~49 günde başa döner |
| `margin_pct` | Belirleyici tepenin eşiği aşma oranı: (tepe − eşik) × 100 / eşik. WEB için tepe \|ω_y\|, ICE için a_lin,z, FIRE için a_lin,x |
| `level` | `LOW`, `MED`, `HIGH` |
| `result` | `OK`, `FAIL` |
| `bx`, `by`, `bz` | Ölçülen gyro bias, ham LSB (65.5 LSB = 1 °/s). FAIL'de ölçülen son değer |
| `fault` | `I2C`, `WHOAMI`, `STUCK`, `OVERRUN`, `NVS` |
| `loop_max_us` | Son 1 s'deki en uzun döngü süresi, µs |
| `overruns` | Açılıştan beri kaçırılan tik sayısı |
| `i2c_errors` | Açılıştan beri I²C hata sayısı |

### Örnekler

```
BOOT,0.1.0,POWERON,0x70
CFG,MED
EVT,FIRE,2310,48215,54
EVT,WEB,620,51340,12
CAL,OK,-34,12,7
ERR,I2C
STAT,612,0,0
```

---

## 2. Ham veri çerçevesi (LOG modu)

Her 5 ms'de bir, **16 byte**. Çok byte'lı sayılar **little-endian**'dır. (Sensör big-endian gönderir; firmware çerçeveye yazarken çevirir.)

| Byte | Alan | Açıklama |
|---|---|---|
| 0 | `sync` | Sabit `0xA5` |
| 1 | `seq` | Sıra sayacı, 0–255, her çerçevede +1, 255'ten sonra 0 |
| 2 | `flags` | Aşağıdaki bit tablosu |
| 3–4 | `ax` | int16, ham LSB |
| 5–6 | `ay` | int16, ham LSB |
| 7–8 | `az` | int16, ham LSB |
| 9–10 | `gx` | int16, ham LSB (bias çıkarılmamış) |
| 11–12 | `gy` | int16, ham LSB |
| 13–14 | `gz` | int16, ham LSB |
| 15 | `chk` | Byte 1–14'ün XOR'u |

Değerler **ham**dır (bias ve ölçek uygulanmamış), böylece PC tarafında farklı ayarlarla yeniden işlenebilir.

### `flags` bitleri

| Bit | Anlam |
|---|---|
| 1:0 | ESP32'nin o örnekteki kararı: 0 yok, 1 WEB, 2 ICE, 3 FIRE (`gesture_t` ile aynı) |
| 2 | Buton işareti: bu örnekte kısa basış algılandı (kaydı etiketlemek için) |
| 3 | Doymuş örnek |
| 4 | Yerçekimi tahmini donuk |
| 7:5 | Ayrılmış, 0 gönderilir |

### Alıcı (pdproto.py) kuralları

1. `0xA5` arayın. Bulunca 16 byte alın, checksum'ı kontrol edin.
2. Checksum tutmazsa (veri içinde tesadüfen `0xA5` geçmiş olabilir) bir byte ilerleyip tekrar arayın.
3. `seq` bir öncekinden +1 değilse aradaki fark kadar çerçeve kayıp sayılır (255 → 0 geçişi normaldir).
4. LOG modunda akıştan önce gelen `BOOT` satırı, ilk `0xA5` aranırken atlanır; içindeki sürüm bilgisi kayda not edilir.

### Bant genişliği

16 byte × 200 Hz = 3200 B/s. 115200 baud 8N1 yaklaşık 11 520 B/s taşır, yani hattın yaklaşık %28'i kullanılır. TX tamponu (1 KB) doluysa çerçeve atılır, örnekleme hiç beklemez. Kayıp, `seq` atlamasından anlaşılır.

---

## Değişiklik kuralı

Bu belgeyi değiştiren aynı commit'te `frame_codec` (C) ve `pdproto.py` (Python) da güncellenir ve testleri geçer. Sürüm numarası 1 artırılır.
