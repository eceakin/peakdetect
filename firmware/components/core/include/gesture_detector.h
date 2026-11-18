/*
 * gesture_detector.h - hareket penceresi durum makinesi
 *
 *            tetik (|a_lin| veya |w| eşiği aştı)
 *   IDLE  ─────────────────────────────────────►  WINDOW   (= ARMED: ilk örnek pencerenin 1. örneği)
 *    ▲                                              │ 12 örnek (60 ms) toplandı -> classifier
 *    │                                     kabul    │    ret
 *    │                       ┌─────────────────────┴──────────┐
 *    │                       ▼                                ▼
 *    │                   COOLDOWN (100 tick = 500 ms)      REARM
 *    │                       │ süre doldu                     │
 *    │                       └──────────────► REARM ◄─────────┘
 *    │                                         │ sinyal 10 örnek (50 ms) art arda
 *    │                                         │ tetik eşiğinin %70'inin altında kaldı
 *    └─────────────────────────────────────────┘
 *
 * REARM (histerezis): hareketin "kuyruğu" (yavaşlama, geri dönüş) yeni bir pencere
 * açmasın diye sinyal sakinleşmeden IDLE'a dönülmez. Tek sakin örnek yetmez:
 * hızlanma ile yavaşlama arasında ivme bir-iki örnek sıfırdan geçer.
 * Sakinlik cooldown sırasında da sayılır; el sakinse cooldown biter bitmez IDLE.
 *
 * Yerçekimi dondurma: WINDOW durumunda detector_wants_freeze() true döner.
 * Sampling döngüsündeki sıra:
 *     gravity_update(&sample, detector_wants_freeze(), &motion);
 *     if (detector_update(&motion, sample.saturated, now, &result)) { ... }
 * Tetikleyen ilk örnekte tahmin henüz donmamıştır; o tek örneğin etkisi 1/64'tür
 * (ör. 800 mg tetikte ~12 mg) ve ihmal edilir.
 *
 * Zaman parametre olarak verilir (now, tick); modül saate dokunmaz.
 * Durum modül içinde static tutulur; testlerde detector_init() ile sıfırlanır.
 */
#ifndef GESTURE_DETECTOR_H
#define GESTURE_DETECTOR_H

#include <stdbool.h>

#include "pd_types.h"

typedef enum {
    DET_IDLE = 0,
    DET_WINDOW,
    DET_COOLDOWN,
    DET_REARM,
} det_state_t;

/* Durumu sıfırlar (IDLE) ve eşikleri yükler */
void detector_init(const thresholds_t *th);

/* Hassasiyet değişince çağrılır; açık bir pencere varsa yeni eşiklerle karar verilir */
void detector_set_thresholds(const thresholds_t *th);

/*
 * Bir örnek işler. Bu örnekte bir pencere tamamlanıp sınıflandırıldıysa true döner
 * ve out doldurulur (kabul: out->gesture != NONE, ret: out->reject dolu).
 * Diğer örneklerde false döner ve out'a dokunulmaz.
 */
bool detector_update(const motion_t *m, bool saturated, pd_tick_t now, gesture_result_t *out);

/* WINDOW durumunda true: gravity_filter'a dondurma isteği */
bool detector_wants_freeze(void);

det_state_t detector_state(void);

/* Son tamamlanan pencerenin tepe değerleri (replay / analiz için) */
const window_peaks_t *detector_last_peaks(void);

#endif /* GESTURE_DETECTOR_H */
