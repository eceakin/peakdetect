#include "imu_convert.h"

#include <stddef.h>

#include "app_config.h"

/*
 * İvme: ölçek ve birim dönüşümü TEK bölmede yapılır.
 *   mg = raw * scale * 125 / (1024 * 512)
 * İki ayrı bölme yapılırsa her biri küsuratı ayrı keser ve hata 1 mg'yi aşar
 * (test_accel_matches_float_reference_everywhere bunu yakalar).
 * Pay en fazla 32768 * 1075 * 125 = 4.4e9 olur; int32_t sınırını (2.1e9) aştığı
 * için ara sonuç int64_t'dir. Örnek başına 3 çarpma, 200 Hz'de önemsiz bir yük.
 *
 * Dönüş: (32768 + 1310) * 20 = 681 560 -> int32_t yeterli.
 * Her iki durumda da önce çarp, sonra böl (hassasiyet).
 */

int32_t imu_accel_to_mg(int16_t raw, int16_t scale_q10)
{
    int64_t num = (int64_t)raw * scale_q10 * PD_ACCEL_MG_NUM;
    int64_t den = (int64_t)PD_SCALE_Q10_ONE * PD_ACCEL_MG_DEN;
    return (int32_t)(num / den);
}

int32_t imu_gyro_to_dps10(int16_t raw, int16_t bias)
{
    int32_t unbiased = (int32_t)raw - (int32_t)bias;
    return (unbiased * PD_GYRO_DPS10_NUM) / PD_GYRO_DPS10_DEN;
}

static bool is_saturated(int16_t v)
{
    return v >= PD_SAT_LIMIT || v <= -PD_SAT_LIMIT;
}

void imu_convert(const imu_raw_t *raw, const calib_t *cal, imu_sample_t *out)
{
    calib_t none;
    if (cal == NULL) {
        calib_set_default(&none);
        cal = &none;
    }

    bool sat = false;
    for (int i = 0; i < 3; i++) {
        sat = sat || is_saturated(raw->accel[i]) || is_saturated(raw->gyro[i]);
        out->a[i] = imu_accel_to_mg(raw->accel[i], cal->accel_scale[i]);
        out->w[i] = imu_gyro_to_dps10(raw->gyro[i], cal->gyro_bias[i]);
    }
    out->saturated = sat;
}

void calib_set_default(calib_t *c)
{
    for (int i = 0; i < 3; i++) {
        c->gyro_bias[i] = 0;
        c->accel_scale[i] = PD_SCALE_Q10_ONE;
    }
}

bool calib_is_valid(const calib_t *c)
{
    if (c == NULL) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        if (c->gyro_bias[i] > PD_GYRO_BIAS_LIMIT_LSB || c->gyro_bias[i] < -PD_GYRO_BIAS_LIMIT_LSB) {
            return false;
        }
        if (c->accel_scale[i] < PD_ACCEL_SCALE_MIN_Q10 || c->accel_scale[i] > PD_ACCEL_SCALE_MAX_Q10) {
            return false;
        }
    }
    return true;
}
