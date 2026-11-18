/*
 * gen_signals - yapay örnek kayıt üretir (replay ve Python araçlarını denemek için)
 *
 * Kullanım:  gen_signals <cikti.csv>
 *
 * İçerik (12 s, 200 Hz, 2400 örnek):
 *   0-1 s   hareketsiz
 *   1 s     WEB  (bilek silkme)    — 0.9 s'de buton işareti
 *   3 s     ICE  (ani kaldırma)    — 2.9 s'de buton işareti
 *   5 s     FIRE (sert itme)       — 4.9 s'de buton işareti
 *   7-12 s  normal kol hareketi (hiç tetik beklenmez)
 *
 * Beklenen replay çıktısı (Medium): WEB ~1.0 s, ICE ~3.0 s, FIRE ~5.1 s; başka olay yok.
 */
#include <stdio.h>

#include "frame_codec.h"
#include "signals.h"

#define N (SIG_HZ * 12)

static imu_raw_t buf[N];
static uint8_t flags[N];

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "kullanim: gen_signals <cikti.csv>\n");
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
    return 0;
}
