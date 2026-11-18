#include "gesture_classifier.h"

#include "app_config.h"

/*
 * Baskınlık "a > 1.5 * b" kesirsiz yazılır: a * 10 > dominance_x10 * b
 * En büyük çarpım ~16 000 * 15 = 240 000 -> int32_t yeterli.
 */
static bool dominant(int32_t a, int32_t b, int32_t dominance_x10)
{
    return a * 10 > dominance_x10 * b;
}

int16_t classifier_margin_pct(int32_t peak, int32_t threshold)
{
    if (threshold <= 0) {
        return 0;
    }
    int32_t m = ((peak - threshold) * 100) / threshold;
    if (m > INT16_MAX) {
        m = INT16_MAX;
    } else if (m < INT16_MIN) {
        m = INT16_MIN;
    }
    return (int16_t)m;
}

static bool web_rotation_ok(const window_peaks_t *p, const thresholds_t *th)
{
    return p->w_abs[AX_Y] >= th->web_w_min;
}

bool classifier_rule_passes(gesture_t g, const window_peaks_t *p, const thresholds_t *th)
{
    switch (g) {
    case GESTURE_WEB:
        return web_rotation_ok(p, th) && p->a_mag < th->web_a_max;
    case GESTURE_ICE:
        return p->a_pos[AX_Z] >= th->ice_a_min &&
               dominant(p->a_pos[AX_Z], p->a_abs[AX_X], th->dominance_x10);
    case GESTURE_FIRE:
        return p->a_pos[AX_X] >= th->fire_a_min &&
               dominant(p->a_pos[AX_X], p->a_abs[AX_Z], th->dominance_x10);
    default:
        return false;
    }
}

static int16_t rule_margin(gesture_t g, const window_peaks_t *p, const thresholds_t *th)
{
    switch (g) {
    case GESTURE_WEB: return classifier_margin_pct(p->w_abs[AX_Y], th->web_w_min);
    case GESTURE_ICE: return classifier_margin_pct(p->a_pos[AX_Z], th->ice_a_min);
    case GESTURE_FIRE: return classifier_margin_pct(p->a_pos[AX_X], th->fire_a_min);
    default: return 0;
    }
}

static void reject(gesture_result_t *out, reject_reason_t why, int16_t margin)
{
    out->gesture = GESTURE_NONE;
    out->reject = why;
    out->margin_pct = margin;
}

void classifier_classify(const window_peaks_t *p, const thresholds_t *th, pd_tick_t t,
                         gesture_result_t *out)
{
    out->peak_mg = p->a_mag;
    out->t = t;

    if (p->saturated) {
        reject(out, REJECT_SATURATED, 0);
        return;
    }

    /* Geçen kuralları ve margin'lerini topla (en fazla 2 tane geçebilir) */
    static const gesture_t order[] = {GESTURE_WEB, GESTURE_ICE, GESTURE_FIRE};
    gesture_t best = GESTURE_NONE, second = GESTURE_NONE;
    int16_t best_m = 0, second_m = 0;

    for (unsigned i = 0; i < sizeof order / sizeof order[0]; i++) {
        if (!classifier_rule_passes(order[i], p, th)) {
            continue;
        }
        int16_t m = rule_margin(order[i], p, th);
        if (best == GESTURE_NONE || m > best_m) {
            second = best;
            second_m = best_m;
            best = order[i];
            best_m = m;
        } else {
            second = order[i];
            second_m = m;
        }
    }

    if (best == GESTURE_NONE) {
        reject(out, web_rotation_ok(p, th) ? REJECT_WEB_TOO_HARD : REJECT_NO_RULE, 0);
        return;
    }

    if (second != GESTURE_NONE && (int32_t)best_m - second_m < PD_AMBIGUITY_GAP_PCT) {
        reject(out, REJECT_AMBIGUOUS, best_m);
        return;
    }

    out->gesture = best;
    out->reject = REJECT_NONE;
    out->margin_pct = best_m;
}
