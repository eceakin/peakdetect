#include "gravity_filter.h"

#include "app_config.h"

/*
 * NOT: Negatif sayının >> ile kaydırılması C standardında derleyiciye bağlıdır.
 * gcc (PC) ve xtensa-gcc (ESP32) aritmetik kaydırma yapar (işaret korunur).
 * test_arithmetic_shift_of_negative bunu sabitler; farklı bir derleyicide
 * test kırılırsa bu dosya gözden geçirilmelidir.
 */

static int32_t s_g_q[3];   /* 64 * mg */
static bool s_has_estimate;

void gravity_reset(void)
{
    for (int i = 0; i < 3; i++) {
        s_g_q[i] = 0;
    }
    s_has_estimate = false;
}

static int32_t abs32(int32_t v)
{
    return v < 0 ? -v : v;
}

uint32_t isqrt_u32(uint32_t v)
{
    /* Bit bit yöntem: döngü 16 tur, bölme yok */
    uint32_t res = 0;
    uint32_t bit = 1u << 30;
    while (bit > v) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (v >= res + bit) {
            v -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

void gravity_update(const imu_sample_t *s, bool freeze_request, motion_t *out)
{
    /* İlk örnek: tahmini doğrudan başlat */
    if (!s_has_estimate) {
        for (int i = 0; i < 3; i++) {
            /* Çarpma, çünkü negatif sayıyı << ile kaydırmak C'de tanımsız davranış */
            s_g_q[i] = s->a[i] * (1 << PD_GRAVITY_SHIFT);
        }
        s_has_estimate = true;
    }

    const int32_t w_sum = abs32(s->w[0]) + abs32(s->w[1]) + abs32(s->w[2]);
    const bool frozen = freeze_request || s->saturated || (w_sum > PD_GRAVITY_FREEZE_W);

    /*
     * |a_lin| üst sınırı: a ve g ±8000 mg içinde -> a_lin ±16 000 mg,
     * kareler toplamı 3 * 16 000^2 = 7.68e8 < 2^32. uint32_t yeterli.
     */
    uint32_t sq = 0;
    for (int i = 0; i < 3; i++) {
        const int32_t g = s_g_q[i] >> PD_GRAVITY_SHIFT; /* önceki tahmin */
        const int32_t lin = s->a[i] - g;
        out->a_lin[i] = lin;
        out->w[i] = s->w[i];
        sq += (uint32_t)(lin * lin);

        if (!frozen) {
            s_g_q[i] += s->a[i] - g;
        }
    }
    out->a_lin_mag = (int32_t)isqrt_u32(sq);
    out->gravity_frozen = frozen;
}

bool gravity_get(int32_t g_mg[3])
{
    for (int i = 0; i < 3; i++) {
        g_mg[i] = s_g_q[i] >> PD_GRAVITY_SHIFT;
    }
    return s_has_estimate;
}
