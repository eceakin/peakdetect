# PeakDetect — Betül için başlangıç rehberi

Bu belge iki şey anlatır: **şimdiye kadar ne yaptık** ve **projeyi kendi bilgisayarında nasıl
çalıştırırsın**. Sonunda üç komut da "başarılı" yazıyorsa her şey hazır demektir.

Yapay zekâ ile çalışırken ona verilecek bağlam dosyası depo kökündeki `CLAUDE.md`'dir (Bölüm 5).

---

## 1. Şimdiye kadar ne yaptık? (özet)

Proje: bileğe takılan ESP32 + MPU-6500 devresi, üç el hareketini tanıyıp LED ve sesle tepki veriyor
(WEB = bilek silkme, ICE = eli kaldırma, FIRE = eli itme). Yazılımı 12 adımlık bir yol haritasıyla
ilerletiyoruz. **Kart olmadan yapılabilen ilk beş adım bitti:**

| Adım | Ne yapıldı |
|---|---|
| 0–1 | Geliştirme ortamı kuruldu; GitHub deposu ve ESP-IDF proje iskeleti oluşturuldu (`core`, `drivers`, `services`, `app` bileşenleri), PC test altyapısı (CMake + Unity) hazır. |
| 2 | Ortak sözleşmeler yazıldı: `pd_types.h` (bütün veri tipleri), `app_config.h` (bütün sayılar), `board_pins.h` (pinler), `docs/protocol.md` (UART protokolü). |
| 3 | Projenin "beyni" yazıldı ve PC'de test edildi: `crc16`, `frame_codec`, `imu_convert`, `sensitivity`, `gravity_filter`, `gesture_classifier`, `gesture_detector`. Ayrıca yapay sensör sinyali üreten `signals.c` ve kayıt oynatan `replay` programı. |
| 4 | PC'deki Python araçları: `pdproto.py` (protokol), `decode.py` (.bin → CSV), `event_check.py` (seri port satırlarını doğrular), `record.py` (karttan ham veri kaydı). C ile Python'un aynı formatı konuştuğu testlerle kanıtlandı. |

Kanıtlanan zincir: yapay sinyal → `.bin` → `decode.py` → CSV → `replay` → **WEB, ICE, FIRE** doğru
zamanlarda bulunuyor, normal kol hareketinde yanlış tetik yok.

**Sıradaki işler:**
- **Adım 6 – Servisler** (kartsız başlanabilir): `button_handler`, `feedback_player`, `settings_store`,
  `calibration`, `event_reporter`, `raw_logger`, `fault_monitor`. Her birinin saf mantığı `core/`'a
  yazılıp PC'de test edilir; donanıma bağlayan ince katman kart gelince.
- **Adım 5 – Kart bring-up:** kart elimize geçince sürücüler tek tek denenir (LED, UART, I²C, sensör,
  200 Hz zamanlayıcı, buton, buzzer, watchdog, veri akışı).

Kim neyi alacak ekipçe kararlaştırılacak; aynı dosya üzerinde iki kişi aynı anda çalışmasın.

---

## 2. Kurulum (Windows, ~1 saat; çoğu indirme beklemesi)

> **Önemli:** Hiçbir şeyi boşluk ya da Türkçe karakter içeren bir klasöre kurma/klonlama
> (ör. `C:\Users\Betül Sarı\Masaüstü` olmaz). Biz her şeyi `C:\esp\` altında tutuyoruz.

Komutları **PowerShell**'e yaz (Başlat → "PowerShell"). Her `winget` kurulumundan sonra PowerShell'i
**kapatıp yeniden aç**, yoksa yeni program görünmez.

### 2.1 Git
```powershell
winget install --id Git.Git -e
git config --global user.name "Betül Sarı"
git config --global user.email "github-hesabindaki@eposta.com"
```

### 2.2 Python (3.10 veya üstü)
```powershell
winget install --id Python.Python.3.12 -e
python --version
```
`python` bulunamıyorsa `py --version` dene; o da yoksa Python'u kaldırıp kurulumda
**"Add python.exe to PATH"** kutusunu işaretleyerek tekrar kur.

### 2.3 PC testleri için C derleyicisi ve CMake
```powershell
winget install --id BrechtSanders.WinLibs.POSIX.UCRT -e
winget install --id Kitware.CMake -e
```
Kontrol (yeni bir PowerShell'de):
```powershell
gcc --version
cmake --version
where.exe gcc
```
`where.exe gcc` ilk satırda **WinLibs** yolunu göstermeli. `C:\MinGW\bin` gibi eski bir derleyici
önce geliyorsa onu PATH'ten kaldır.

### 2.4 VS Code + ESP-IDF v5.5.6
1. `winget install --id Microsoft.VisualStudioCode -e`
2. VS Code → sol menü **Extensions** (Ctrl+Shift+X) → **"ESP-IDF"** (Espressif) eklentisini kur.
3. Ctrl+Shift+P → **"ESP-IDF: Open ESP-IDF Installation Manager"** → açılan kurulum programında
   (EIM) özel/gelişmiş kurulumu seç ve sürüm olarak **v5.5.6**'yı işaretle (en yeni sürüm değil,
   ekipçe hepimiz aynı sürümü kullanıyoruz). Kurulum yolu: `C:\esp` gibi boşluksuz bir yol.
   Kurulum 20–40 dakika sürebilir.
4. Bittiğinde VS Code'u yeniden aç → Ctrl+Shift+P → **"ESP-IDF: Open ESP-IDF Terminal"** →
   ```
   idf.py --version
   ```
   `ESP-IDF v5.5.6` yazmalı.

### 2.5 GitHub erişimi
1. Bir GitHub hesabın yoksa github.com'dan aç; kullanıcı adını Ece'ye gönder.
2. Ece seni depoya **collaborator** olarak ekleyecek. E-postana (ya da github.com/notifications'a)
   gelen daveti **Accept** ile kabul et. Depo özel (private) olduğu için bu adım şart.

### 2.6 Depoyu indir ve Python paketlerini kur
```powershell
mkdir C:\esp -Force
cd C:\esp
git clone https://github.com/eceakin/peakdetect.git
cd peakdetect
python -m pip install -r tools/requirements.txt
```
İlk `git clone`'da tarayıcı açılıp GitHub'a giriş yapmanı isteyebilir; giriş yap ve izin ver.

### 2.7 (Kart gelince) USB sürücüsü
Kartı **veri taşıyan** bir USB kabloyla tak → Aygıt Yöneticisi → **Bağlantı noktaları (COM ve LPT)**
altında bir COM portu görünmeli. Görünmüyorsa kartın üstündeki USB çipine göre sürücü kur:
**CP210x** (Silicon Labs) ya da **CH340** (WCH).

---

## 3. Her şey çalışıyor mu? (üç kontrol)

**A) PC testleri** — normal PowerShell, depo kökünde:
```powershell
cd C:\esp\peakdetect
cmake -S host_tests -B build_host -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc
cmake --build build_host
ctest --test-dir build_host --output-on-failure
```
Beklenen: **`100% tests passed`**. (İlk komut Unity'yi GitHub'dan indirir, internet gerekir.
Sonraki seferlerde yalnızca son iki komut yeterli.)

**B) Python araç testleri** — A'dan sonra, aynı yerde:
```powershell
python -m unittest discover tools/tests -v
```
Beklenen: sonda **`OK`**.

**C) Firmware** — VS Code → **ESP-IDF: Open ESP-IDF Terminal**:
```powershell
cd C:\esp\peakdetect\firmware
idf.py build
```
Beklenen: **`Project build complete`**. İlk derleme birkaç dakika sürer. Hedefle ilgili hata
alırsan bir kez `idf.py set-target esp32` çalıştırıp tekrar dene.

Üçü de geçtiyse kurulum bitti 🎉. Takılırsan hata çıktısının tamamını kopyalayıp ekibe gönder.

> İki terminali karıştırma: `idf.py` **yalnızca** ESP-IDF terminalinde; `cmake/ctest/git/python`
> **normal** PowerShell'de. ESP-IDF terminalinde `cmake` çalıştırırsan PC testleri derlenmez.

---

## 4. Günlük çalışma düzeni (git)

```powershell
cd C:\esp\peakdetect
git checkout main
git pull                                   # her işe başlamadan önce güncel hali al
git checkout -b service/button_handler     # her iş için yeni dal
# ... kodla, testleri çalıştır (Bölüm 3-A) ...
git add .
git commit -m "services: button_handler saf mantik + testler"
git push -u origin service/button_handler
```
Sonra GitHub'da depo sayfasında çıkan **"Compare & pull request"** düğmesiyle Pull Request aç;
ekipten biri bakıp `main`'e birleştirir. `main` dalına doğrudan push etme: `main` her zaman
derlenen ve testleri geçen halde kalmalı.

Kurallar kısaca: sayılar yalnızca `app_config.h` / `board_pins.h`'te; yorumlar Türkçe ve birimli;
`pd_types.h`, `app_config.h` veya protokolü değiştireceksen önce ekibe haber ver.
Ayrıntılar `CLAUDE.md` Bölüm 6'da.

---

## 5. Claude ile çalışırken

Depo kökündeki **`CLAUDE.md`** projenin mimarisini, bitmiş modülleri, kodlama kurallarını,
yol haritasındaki durumu ve beklenen teslim biçimini anlatır.

- **Claude Code** (VS Code eklentisi ya da terminal) kullanıyorsan: `C:\esp\peakdetect` klasörünü aç;
  `CLAUDE.md` otomatik okunur. Ek bir şey yapmana gerek yok.
- **claude.ai** (web/masaüstü) kullanıyorsan: yeni bir Project aç, GitHub deposunu projeye kaynak
  olarak bağla (ya da `CLAUDE.md` ve ilgili dosyaları yükle) ve `CLAUDE.md`'nin içeriğini projenin
  talimatlar (instructions) alanına yapıştır.

İyi bir istek örneği:
> "CLAUDE.md'deki kurallara göre Adım 6.1 button_handler servisinin saf mantığını core/ içine yaz.
> Zıplayan sinyalden tek SHORT, 1 s basılı tutunca tek LONG, açılışta basılıysa HELD_AT_BOOT.
> .h, .c ve test dosyasını ver, app_config.h ve CMake satırlarını ayrıca yaz, derleyip testleri çalıştır."

Claude'un verdiği kodu da mutlaka kendi bilgisayarında Bölüm 3-A ile test et, sonra commit'le.
Yeni bir modül bitince `CLAUDE.md`'deki durum tablosunu (Bölüm 8) güncellemeyi unutma.
