# PeakDetect – ESP32 Wrist Gesture Controller

CENG424 Embedded System Design dönem projesi. MPU-6500 IMU ile üç bilek hareketini
(Web Shot, Ice Shield, Fireball) tanıyan, LED ve buzzer ile tepki veren ESP32 firmware'i.

Ekip: Muhammet Yasin Uysal (lider), Betül Sarı, Ece Akın

## Klasör yapısı

| Klasör | İçerik |
|---|---|
| `firmware/` | ESP-IDF projesi (`main` + `core`, `drivers`, `services`, `app` bileşenleri) |
| `host_tests/` | `core` bileşeninin PC'de çalışan Unity testleri |
| `bringup/` | Her sürücü için tek başına çalışan deneme programları |
| `tools/` | PC tarafı Python araçları (kayıt, çözme, analiz) |
| `data/` | CSV'ye çevrilmiş kayıtlar (ham `.bin` dosyaları `data/raw/` altında, depoya girmez) |
| `docs/` | Protokol belgesi ve test kayıtları (`docs/test_records/`) |

## Gereksinimler

- ESP-IDF **v5.5.6** + VS Code ESP-IDF eklentisi
- GCC (WinLibs, `winget install BrechtSanders.WinLibs.POSIX.UCRT`) ve CMake ≥ 3.16
- Python ≥ 3.10, Git

> **Önemli:** Depoyu boşluk ve Türkçe karakter içermeyen bir yola klonlayın
> (ör. `C:\esp\peakdetect`). Aksi halde ESP-IDF derlemesi bozulur.

## Hangi komut nerede çalışır?

| Komut | Terminal |
|---|---|
| `idf.py ...` | VS Code → **ESP-IDF: Open ESP-IDF Terminal** |
| `cmake`, `ctest`, `gcc`, `git`, `python` | Normal PowerShell |

İki ortamı karıştırmayın: ESP-IDF terminalinde `cmake` çalıştırırsanız CMake
ESP32 derleyicisini seçer ve PC testleri derlenmez.

## Kurulum

```powershell
cd C:\esp
git clone https://github.com/<kullanici>/peakdetect.git
cd peakdetect
python -m pip install -r tools/requirements.txt
```

## Firmware derleme (ESP-IDF terminali)

```powershell
cd C:\esp\peakdetect\firmware
idf.py build
```
Başarılıysa sonda `Project build complete` yazar.

Karta yükleme ve seri monitör (COM numarası Aygıt Yöneticisi'nde görünür):
```powershell
idf.py -p COM5 flash monitor
```
Monitörden çıkış: **Ctrl + ]**. Yükleme `Connecting...` aşamasında takılırsa kartın **BOOT** tuşunu basılı tutun.

## PC testleri (normal PowerShell)

```powershell
cd C:\esp\peakdetect
cmake -S host_tests -B build_host -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc
cmake --build build_host
ctest --test-dir build_host --output-on-failure
```
Başarılıysa `100% tests passed` yazar. İlk komut yalnızca bir kez gerekir; sonraki
seferlerde son iki komut yeterlidir.

## Sık karşılaşılan sorunlar

| Belirti | Çözüm |
|---|---|
| `CMakeLists.txt not found in project directory` | `idf.py`'ı `firmware/` klasörünün içinde çalıştırın |
| CMake derleyici olarak `esp-clang` seçiyor | Normal PowerShell kullanın; `build_host`'u silip `-DCMAKE_C_COMPILER=gcc` ile tekrar deneyin |
| `where.exe gcc` önce `C:\MinGW\bin` gösteriyor | Eski MinGW'yi PATH'ten kaldırın ya da WinLibs'in altına taşıyın |
| Generator hatası ("does not match the generator used previously") | `build_host` klasörünü silin |

## Çalışma kuralları

- `main` dalı her zaman derlenir ve testleri geçer; her modül için ayrı dal açın.
- Eşikler, süreler ve pin numaraları kodun içine yazılmaz: `app_config.h` / `board_pins.h`.
- Kartlı her testin kanıtı tarihle `docs/test_records/` altına konur.
python -m unittest discover tools/tests -v