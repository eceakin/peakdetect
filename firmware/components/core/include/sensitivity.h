/*
 * sensitivity.h - eşik tablosu ve Low / Medium / High ölçeklemesi
 *
 * Temel (Medium) değerler app_config.h'deki PD_TH_* sabitlerinden gelir.
 * Adım 8'de eşikler veriyle güncellenirken sadece app_config.h değişir.
 *
 * Ölçekleme kuralı (Adım 2, karar 4):
 *   Ölçeklenen   : trigger_mg, trigger_w, web_w_min, ice_a_min, fire_a_min  (alt eşikler)
 *   Ölçeklenmeyen: web_a_max      (üst sınır; ölçeklense Low'da Web kolaylaşırdı)
 *                  dominance_x10  (oran; büyüklükten bağımsız)
 *
 *   Low  = alt eşikler x1.30 -> daha sert hareket gerekir
 *   High = alt eşikler x0.75 -> daha hafif hareket yeter
 */
#ifndef SENSITIVITY_H
#define SENSITIVITY_H

#include <stdbool.h>
#include <stdint.h>

#include "pd_types.h"

/* Medium tablosu (ölçeksiz) */
void sensitivity_base(thresholds_t *out);

/* Seçili seviyeye göre ölçeklenmiş eşikler. Geçersiz seviye -> Medium. */
void sensitivity_apply(sensitivity_t s, thresholds_t *out);

/* Seviyenin yüzde çarpanı: 130 / 100 / 75. Geçersiz seviye -> 100. */
int32_t sensitivity_pct(sensitivity_t s);

/* Kısa basışta sıradaki seviye: LOW -> MED -> HIGH -> LOW. Geçersiz -> MED. */
sensitivity_t sensitivity_next(sensitivity_t s);

/* NVS'ten okunan byte geçerli bir seviye mi */
bool sensitivity_is_valid(uint8_t v);

#endif /* SENSITIVITY_H */
