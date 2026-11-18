/*
 * imu_convert.h - ham sensör sayıları -> fiziksel birimler
 *
 * Sıra:
 *   1) Doyma kontrolü (ham değer üzerinde, düzeltmeden önce)
 *   2) İvme:  raw * accel_scale * 125 / (1024 * 512)   tek bölme, int64 ara sonuç
 *             (Q10 ölçek düzeltmesi + LSB -> mg; ±8 g'de 4096 LSB = 1000 mg)
 *   3) Dönüş: (raw - gyro_bias) * 20 / 131       (LSB -> 0.1 °/s, 65.5 LSB = 1 °/s)
 *
 * Tamamen tamsayı aritmetiği: ESP32'de ve PC'de bit bit aynı sonuç.
 * Bölmeler sıfıra doğru keser (C99), yani f(-x) == -f(x).
 */
#ifndef IMU_CONVERT_H
#define IMU_CONVERT_H

#include <stdbool.h>

#include "pd_types.h"

/* raw + cal -> out. cal NULL ise düzeltme yok (bias 0, ölçek 1.0). */
void imu_convert(const imu_raw_t *raw, const calib_t *cal, imu_sample_t *out);

/* Tek değer dönüşümleri (testlerde ve offline analizde de kullanılır) */
int32_t imu_accel_to_mg(int16_t raw, int16_t scale_q10);
int32_t imu_gyro_to_dps10(int16_t raw, int16_t bias);

/* Kalibrasyon yardımcıları */
void calib_set_default(calib_t *c);     /* bias 0, ölçek 1024 */
bool calib_is_valid(const calib_t *c);  /* app_config.h sınırları içinde mi */

#endif /* IMU_CONVERT_H */
