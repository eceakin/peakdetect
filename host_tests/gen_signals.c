/*
 * gen_signals - yapay örnek kayıt üretir (replay ve Python araçlarını denemek için)
 *
 * Kullanım:  gen_signals <cikti.csv> [cikti.bin]
 *
 *   .csv : decode.py çıktısıyla aynı biçim (seq,flags,ax,ay,az,gx,gy,gz)
 *   .bin : kartın LOG modunda gönderdiği akışla aynı: önce bir BOOT satırı, sonra
 *          16 byte'lık çerçeveler (frame_codec ile paketlenmiş). decode.py ile
 *          CSV'ye çevrilip replay'e verildiğinde .csv ile AYNI sonucu vermeli.
 *
 * İçerik (12 s, 200 Hz, 2400 örnek):
 *   0-1 s   hareketsiz
 *   1 s     WEB  (bilek silkme)    — 0.9 s'de buton işareti
 *   3 s     ICE  (ani kaldırma)    — 2.9 s'de buton işareti
 *   5 s     FIRE (sert itme)       — 4.9 s'de buton işareti
 *   7-12 s  normal kol hareketi (hiç tetik beklenmez)
 *
 * Beklenen replay çıktısı (Medium): WEB ~1.07 s, ICE ~3.06 s, FIRE ~5.06 s; başka olay yok.
 */
#include <stdio.h>

#include "app_config.h"
#include "frame_codec.h"
#include "signals.h"

#define N (SIG_HZ * 12)

static imu_raw_t buf[N];
static uint8_t flags[N];

static int write_bin(const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        perror(path);
        return 1;
    }
    char line[PD_LINE_MAX + 1];
    int n = line_boot(line, sizeof line, PD_FW_VERSION, "POWERON", 0x70);
    fwrite(line, 1, (size_t)n, fp);

    for (int i = 0; i < N; i++) {
        log_frame_t f;
        uint8_t out[PD_FRAME_LEN];
        frame_from_raw(&buf[i], (uint8_t)i, flags[i], &f);
        frame_pack(&f, out);
        fwrite(out, 1, sizeof out, fp);
    }
    fclose(fp);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "kullanim: gen_signals <cikti.csv> [cikti.bin]\n");
        return 2;
    }

    signals_t s;
    sig_begin(&s, buf, N, 10, 2026);
    sig_gesture(&s, SIG_HZ * 1, GESTURE_WEB, 100);
    sig_gesture(&s, SIG_HZ * 3, GESTURE_ICE, 100);
    sig_gesture(&s, SIG_HZ * 5, GESTURE_FIRE, 100);
    sig_normal_motion(&s, SIG_HZ * 7, SIG_HZ * 5);
    sig_end(&s);

    /* Adım 8'deki kayıt protokolü gibi: her hareketten 100 ms önce buton işareti */
    flags[SIG_HZ * 1 - SIG_MS(100)] = PD_FLAG_BUTTON;
    flags[SIG_HZ * 3 - SIG_MS(100)] = PD_FLAG_BUTTON;
    flags[SIG_HZ * 5 - SIG_MS(100)] = PD_FLAG_BUTTON;

    FILE *fp = fopen(argv[1], "w");
    if (fp == NULL) {
        perror(argv[1]);
        return 1;
    }
    sig_write_csv(buf, N, flags, fp);
    fclose(fp);
    printf("%s yazildi: %d ornek (%.1f s). Beklenen: WEB, ICE, FIRE.\n", argv[1], N, N / 200.0);

    if (argc == 3) {
        if (write_bin(argv[2]) != 0) {
            return 1;
        }
        printf("%s yazildi: BOOT satiri + %d cerceve.\n", argv[2], N);
    }
    return 0;
}
