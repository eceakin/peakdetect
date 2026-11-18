/*
 * gesture_classifier.h - pencere tepe değerlerinden karar
 *
 * Girdi : 60 ms'lik pencerenin tepe değerleri (window_peaks_t) + eşikler (thresholds_t)
 * Çıktı : WEB / ICE / FIRE ya da ret sebebi, margin ve tepe ivme
 *
 * Kurallar (eşikler sensitivity_apply ile seçili seviyeye göre gelir):
 *
 *   WEB  : w_abs[Y] >= web_w_min   VE  a_mag < web_a_max          (dönüş baskın)
 *   ICE  : a_pos[Z] >= ice_a_min   VE  a_pos[Z] > 1.5 * a_abs[X]   (yukarı baskın)
 *   FIRE : a_pos[X] >= fire_a_min  VE  a_pos[X] > 1.5 * a_abs[Z]   (ileri baskın)
 *
 *   - Yalnızca POZİTİF yön sayılır: eli aşağı indirmek ICE, geri çekmek FIRE değildir.
 *   - Baskınlıkta karşı eksenin MUTLAK tepesi kullanılır (o eksende hangi yönde olursa olsun).
 *   - ICE ve FIRE matematiksel olarak aynı anda geçemez; WEB ise ikisinden biriyle geçebilir.
 *
 * Karar sırası:
 *   1) Pencerede doymuş örnek varsa                -> REJECT_SATURATED
 *   2) Hiçbir kural geçmediyse:
 *        WEB'in dönüş şartı sağlanmış ama ivme fazlaysa -> REJECT_WEB_TOO_HARD
 *        değilse                                        -> REJECT_NO_RULE
 *   3) Tek kural geçtiyse                          -> o hareket
 *   4) İki kural geçtiyse: margin farkı >= PD_AMBIGUITY_GAP_PCT ise büyük margin
 *      kazanır, değilse -> REJECT_AMBIGUOUS
 *
 * margin_pct = (belirleyici tepe - eşik) * 100 / eşik
 *   belirleyici tepe: WEB -> w_abs[Y], ICE -> a_pos[Z], FIRE -> a_pos[X]
 *
 * Durumsuzdur (static değişken yok), aynı girdi her zaman aynı sonucu verir.
 */
#ifndef GESTURE_CLASSIFIER_H
#define GESTURE_CLASSIFIER_H

#include <stdbool.h>
#include <stdint.h>

#include "pd_types.h"

void classifier_classify(const window_peaks_t *p, const thresholds_t *th, pd_tick_t t,
                         gesture_result_t *out);

/* Tek bir kuralın geçip geçmediği (analiz araçları ve testler için) */
bool classifier_rule_passes(gesture_t g, const window_peaks_t *p, const thresholds_t *th);

/* (peak - threshold) * 100 / threshold, int16 aralığına kırpılır. threshold <= 0 ise 0. */
int16_t classifier_margin_pct(int32_t peak, int32_t threshold);

#endif /* GESTURE_CLASSIFIER_H */
