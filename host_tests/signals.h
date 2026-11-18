/*
 * signals.h - yapay IMU sinyalleri (yol haritası 3.8)
 *
 * Çıktı imu_raw_t dizisidir, yani gerçek sensör verisiyle aynı biçim (ham LSB, 200 Hz).
 * Böylece detector testleri ve replay programı gerçek kayıtla yapay sinyali aynı
 * yoldan işler.
 *
 * Kullanım:
 *     imu_raw_t buf[N];
 *     signals_t s;
 *     sig_begin(&s, buf, N, 10, 1);              // hareketsiz el, ±10 LSB gürültü
 *     sig_gesture(&s, 200, GESTURE_FIRE, 100);   // 1. saniyede sert itme
 *     sig_end(&s);                               // yerçekimini ekler, int16'ya kırpar
 *
 * Fiziksel model: sensör elin sırtında, başlangıçta Z yukarı. Hareketlerin ivmesi
 * ve dönüşü üst üste eklenir. sig_end() Y eksenindeki dönüşü zaman içinde
 * toplayarak (açıyı bularak) yerçekiminin X ve Z'ye nasıl dağıldığını hesaplar;
 * bilek silkmesindeki "yerçekimi sızıntısı" bu sayede gerçekçi olur.
 */
#ifndef SIGNALS_H
#define SIGNALS_H

#include <stdint.h>
#include <stdio.h>

#include "pd_types.h"

#define SIG_HZ 200
#define SIG_MS(ms) ((ms) * SIG_HZ / 1000) /* ms -> örnek sayısı */

typedef struct {
    imu_raw_t *buf;
    int n;
    double *ax, *ay, *az; /* hareket ivmesi, g (yerçekimi hariç) */
    double *gx, *gy, *gz; /* dönüş, °/s */
    unsigned seed;
    int noise_lsb;
} signals_t;

/* Hareketsiz el + gürültü ile başlar (bellek ayırır) */
void sig_begin(signals_t *s, imu_raw_t *buf, int n, int noise_lsb, unsigned seed);

/* Yarım sinüs darbe ekler. kind: 'a' ivme (g) ya da 'g' dönüş (°/s); axis: AX_X/Y/Z */
void sig_pulse(signals_t *s, int start, int len, char kind, int axis, double peak);

/*
 * Hareket ekler. strength_pct = 100 standart şiddet:
 *   WEB : Y dönüşü 400 °/s, 60 ms ileri + 60 ms geri dönüş
 *   ICE : Z ivmesi +2.2 g 60 ms, ardından -1.1 g 120 ms yavaşlama
 *   FIRE: X ivmesi +2.5 g 60 ms, ardından -1.25 g 120 ms yavaşlama
 * Dönüş: hareketin bittiği örnek indeksi.
 */
int sig_gesture(signals_t *s, int start, gesture_t g, int strength_pct);

/* Normal kol hareketi: yavaş (0.4-1.3 Hz), düşük genlikli dalgalanmalar */
void sig_normal_motion(signals_t *s, int start, int len);

/* Yerçekimini ekler, gürültüyü ekler, LSB'ye çevirir, int16'ya kırpar, belleği bırakır */
void sig_end(signals_t *s);

/*
 * Ham örnekleri decode.py ile aynı CSV biçiminde yazar:
 *     seq,flags,ax,ay,az,gx,gy,gz
 * flags NULL ise hepsi 0 (yoksa örnek başına flags byte'ı, ör. buton işareti bit 2).
 */
void sig_write_csv(const imu_raw_t *buf, int n, const uint8_t *flags, FILE *fp);

#endif /* SIGNALS_H */
