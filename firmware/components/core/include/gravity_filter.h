/*
 * gravity_filter.h - yerçekimi tahmini ve doğrusal ivme
 *
 * Sensör yerçekimini de ölçer (masada dururken Z = +1000 mg). Hareketi görmek için
 * yerçekimi tahmin edilip çıkarılır:
 *
 *     g_est  <- g_est + (a - g_est) / 64       (üstel ortalama, ~64 örnek = ~0.32 s)
 *     a_lin   = a - g_est
 *
 * Tamsayıda hassasiyet kaybolmasın diye tahmin içeride 64 kat büyütülmüş tutulur
 * (g_q = 64 * g_est). Güncelleme: g_q += a - (g_q >> 6), çıktı: g_q >> 6.
 * Bu biçimde kararlı durumda çıktı girişe TAM eşit olur (kalıcı 1 mg hata yok).
 *
 * Dondurma: aşağıdakilerden biri varsa g_est o örnekte güncellenmez.
 *   - çağıran istedi (gesture_detector ARMED / WINDOW durumunda)
 *   - |wx| + |wy| + |wz| > PD_GRAVITY_FREEZE_W  (hızlı dönüş: yerçekimi "sızmasın")
 *   - örnek doymuş
 *
 * a_lin her zaman örnekten ÖNCEKİ tahminle hesaplanır; böylece hareketin ilk
 * örneği de hareketten hemen önceki yerçekimine göre ölçülür.
 *
 * Durum modül içinde static tutulur; testler arasında gravity_reset() çağrılır.
 */
#ifndef GRAVITY_FILTER_H
#define GRAVITY_FILTER_H

#include <stdbool.h>
#include <stdint.h>

#include "pd_types.h"

/* Tahmini unutur. Sonraki ilk örnek tahmini doğrudan başlatır (geçiş süresi yok). */
void gravity_reset(void);

/* Bir örnek işler. freeze_request: detector'ın dondurma isteği. */
void gravity_update(const imu_sample_t *s, bool freeze_request, motion_t *out);

/* Güncel yerçekimi tahmini, mg. Henüz örnek gelmediyse false. */
bool gravity_get(int32_t g_mg[3]);

/* Tamsayı karekök: floor(sqrt(v)). |a_lin| için; testte de kullanılır. */
uint32_t isqrt_u32(uint32_t v);

#endif /* GRAVITY_FILTER_H */
