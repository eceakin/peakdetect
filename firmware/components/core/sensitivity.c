#include "sensitivity.h"

#include "app_config.h"

/* En büyük çarpım: 3500 * 130 = 455 000 -> int32_t yeterli */
static int32_t scale(int32_t value, int32_t pct)
{
    return (value * pct) / 100;
}

bool sensitivity_is_valid(uint8_t v)
{
    return v == SENS_LOW || v == SENS_MED || v == SENS_HIGH;
}

int32_t sensitivity_pct(sensitivity_t s)
{
    switch (s) {
    case SENS_LOW: return PD_SENS_LOW_PCT;
    case SENS_HIGH: return PD_SENS_HIGH_PCT;
    case SENS_MED: /* düşer */
    default: return PD_SENS_MED_PCT;
    }
}

sensitivity_t sensitivity_next(sensitivity_t s)
{
    switch (s) {
    case SENS_LOW: return SENS_MED;
    case SENS_MED: return SENS_HIGH;
    case SENS_HIGH: return SENS_LOW;
    default: return SENS_MED;
    }
}

void sensitivity_base(thresholds_t *out)
{
    out->trigger_mg = PD_TH_TRIGGER_MG;
    out->trigger_w = PD_TH_TRIGGER_W;
    out->web_w_min = PD_TH_WEB_W_MIN;
    out->web_a_max = PD_TH_WEB_A_MAX;
    out->ice_a_min = PD_TH_ICE_A_MIN;
    out->fire_a_min = PD_TH_FIRE_A_MIN;
    out->dominance_x10 = PD_TH_DOMINANCE_X10;
}

void sensitivity_apply(sensitivity_t s, thresholds_t *out)
{
    const int32_t pct = sensitivity_pct(s);
    sensitivity_base(out);

    /* alt eşikler */
    out->trigger_mg = scale(out->trigger_mg, pct);
    out->trigger_w = scale(out->trigger_w, pct);
    out->web_w_min = scale(out->web_w_min, pct);
    out->ice_a_min = scale(out->ice_a_min, pct);
    out->fire_a_min = scale(out->fire_a_min, pct);

    /* web_a_max ve dominance_x10 bilinçli olarak ölçeklenmez */
}
