#include "replay.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "frame_codec.h"
#include "gesture_detector.h"
#include "gravity_filter.h"
#include "imu_convert.h"
#include "sensitivity.h"

#define LINE_MAX_LEN 512
#define MAX_COLS 32

enum { C_AX, C_AY, C_AZ, C_GX, C_GY, C_GZ, C_FLAGS, C_COUNT };
static const char *COL_NAMES[C_COUNT] = {"ax", "ay", "az", "gx", "gy", "gz", "flags"};

void replay_default_opts(replay_opts_t *o)
{
    o->sens = SENS_MED;
    calib_set_default(&o->calib);
    o->all_windows = false;
}

const char *replay_reason_name(reject_reason_t r)
{
    switch (r) {
    case REJECT_NONE: return "NONE";
    case REJECT_NO_RULE: return "NO_RULE";
    case REJECT_AMBIGUOUS: return "AMBIGUOUS";
    case REJECT_SATURATED: return "SATURATED";
    case REJECT_WEB_TOO_HARD: return "WEB_TOO_HARD";
    default: return "?";
    }
}

static void strip_eol(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) {
        s[--n] = '\0';
    }
}

/* Satırı virgüllerden böler (yerinde). Alan sayısını döndürür. */
static int split(char *line, char *fields[MAX_COLS])
{
    int n = 0;
    char *p = line;
    while (n < MAX_COLS) {
        fields[n++] = p;
        char *c = strchr(p, ',');
        if (c == NULL) {
            break;
        }
        *c = '\0';
        p = c + 1;
    }
    return n;
}

static bool parse_long(const char *s, long lo, long hi, long *out)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || errno != 0 || v < lo || v > hi) {
        return false;
    }
    *out = v;
    return true;
}

int replay_run(FILE *in, FILE *out, const replay_opts_t *o, replay_stats_t *st)
{
    char line[LINE_MAX_LEN];
    char *f[MAX_COLS];
    int col[C_COUNT];

    memset(st, 0, sizeof *st);

    /* --- başlık --- */
    if (fgets(line, sizeof line, in) == NULL) {
        return -1;
    }
    strip_eol(line);
    int nf = split(line, f);
    for (int c = 0; c < C_COUNT; c++) {
        col[c] = -1;
        for (int i = 0; i < nf; i++) {
            if (strcmp(f[i], COL_NAMES[c]) == 0) {
                col[c] = i;
            }
        }
        if (col[c] < 0 && c != C_FLAGS) {
            fprintf(stderr, "replay: '%s' sutunu yok\n", COL_NAMES[c]);
            return -1;
        }
    }

    /* --- zinciri hazırla --- */
    thresholds_t th;
    sensitivity_apply(o->sens, &th);
    detector_init(&th);
    gravity_reset();

    if (o->all_windows) {
        fprintf(out, "t_ms,result,reason,margin_pct,peak_mg,ax_pos,ax_abs,az_pos,az_abs,"
                     "wy_abs,since_marker_ms\n");
    } else {
        fprintf(out, "t_ms,gesture,margin_pct\n");
    }

    long last_marker = -1;
    pd_tick_t tick = 0;

    /* --- satırlar --- */
    while (fgets(line, sizeof line, in) != NULL) {
        strip_eol(line);
        if (line[0] == '\0') {
            continue; /* boş satır: örnek değil */
        }
        nf = split(line, f);

        imu_raw_t raw = {0};
        long v[C_COUNT] = {0};
        bool ok = true;
        for (int c = 0; c < C_COUNT && ok; c++) {
            if (col[c] < 0) {
                continue;
            }
            if (col[c] >= nf) {
                ok = false;
            } else if (c == C_FLAGS) {
                ok = parse_long(f[col[c]], 0, 255, &v[c]);
            } else {
                ok = parse_long(f[col[c]], -32768, 32767, &v[c]);
            }
        }
        if (!ok) {
            st->bad_lines++;
            tick++; /* kayıp örnek de 5 ms'dir: zaman kaymasın */
            continue;
        }
        for (int i = 0; i < 3; i++) {
            raw.accel[i] = (int16_t)v[C_AX + i];
            raw.gyro[i] = (int16_t)v[C_GX + i];
        }
        if (col[C_FLAGS] >= 0 && (v[C_FLAGS] & PD_FLAG_BUTTON)) {
            last_marker = (long)tick;
        }

        /* --- kartla aynı sıra --- */
        imu_sample_t s;
        motion_t m;
        gesture_result_t r;
        imu_convert(&raw, &o->calib, &s);
        gravity_update(&s, detector_wants_freeze(), &m);
        if (detector_update(&m, s.saturated, tick, &r)) {
            st->windows++;
            const unsigned long t_ms = (unsigned long)r.t * PD_TICK_MS;
            if (r.gesture != GESTURE_NONE) {
                st->events[r.gesture]++;
            } else if ((int)r.reject < 5) {
                st->rejects[r.reject]++;
            }

            if (o->all_windows) {
                const window_peaks_t *p = detector_last_peaks();
                long since = last_marker < 0 ? -1 : ((long)tick - last_marker) * PD_TICK_MS;
                fprintf(out, "%lu,%s,%s,%d,%ld,%ld,%ld,%ld,%ld,%ld,%ld\n", t_ms,
                        r.gesture != GESTURE_NONE ? gesture_name(r.gesture) : "REJECT",
                        replay_reason_name(r.reject), (int)r.margin_pct, (long)r.peak_mg,
                        (long)p->a_pos[AX_X], (long)p->a_abs[AX_X], (long)p->a_pos[AX_Z],
                        (long)p->a_abs[AX_Z], (long)p->w_abs[AX_Y], since);
            } else if (r.gesture != GESTURE_NONE) {
                fprintf(out, "%lu,%s,%d\n", t_ms, gesture_name(r.gesture), (int)r.margin_pct);
            }
        }
        st->frames++;
        tick++;
    }
    return 0;
}
