#include "signals.h"

#include <math.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define ACC_LSB_PER_G 4096.0
#define GYRO_LSB_PER_DPS 65.5

static double *zeros(int n)
{
    double *p = calloc((size_t)n, sizeof(double));
    if (p == NULL) {
        abort();
    }
    return p;
}

void sig_begin(signals_t *s, imu_raw_t *buf, int n, int noise_lsb, unsigned seed)
{
    s->buf = buf;
    s->n = n;
    s->noise_lsb = noise_lsb;
    s->seed = seed;
    s->ax = zeros(n);
    s->ay = zeros(n);
    s->az = zeros(n);
    s->gx = zeros(n);
    s->gy = zeros(n);
    s->gz = zeros(n);
}

static double *channel(signals_t *s, char kind, int axis)
{
    if (kind == 'a') {
        return axis == AX_X ? s->ax : axis == AX_Y ? s->ay : s->az;
    }
    return axis == AX_X ? s->gx : axis == AX_Y ? s->gy : s->gz;
}

void sig_pulse(signals_t *s, int start, int len, char kind, int axis, double peak)
{
    double *ch = channel(s, kind, axis);
    for (int k = 0; k < len; k++) {
        int i = start + k;
        if (i >= 0 && i < s->n) {
            ch[i] += peak * sin(M_PI * (k + 0.5) / len);
        }
    }
}

int sig_gesture(signals_t *s, int start, gesture_t g, int strength_pct)
{
    const double k = strength_pct / 100.0;
    const int L = SIG_MS(60); /* 12 örnek */

    switch (g) {
    case GESTURE_WEB:
        /* ileri silkme + geri dönüş: açı başa döner. Küçük merkezcil ivme X'te. */
        sig_pulse(s, start, L, 'g', AX_Y, 400.0 * k);
        sig_pulse(s, start + L, L, 'g', AX_Y, -400.0 * k);
        sig_pulse(s, start, 2 * L, 'a', AX_X, 0.3 * k);
        return start + 2 * L;
    case GESTURE_ICE:
        sig_pulse(s, start, L, 'a', AX_Z, 2.2 * k);
        sig_pulse(s, start + L, 2 * L, 'a', AX_Z, -1.1 * k);
        return start + 3 * L;
    case GESTURE_FIRE:
        sig_pulse(s, start, L, 'a', AX_X, 2.5 * k);
        sig_pulse(s, start + L, 2 * L, 'a', AX_X, -1.25 * k);
        return start + 3 * L;
    default:
        return start;
    }
}

void sig_normal_motion(signals_t *s, int start, int len)
{
    /* Farklı frekanslar: klavye, yürüme salınımı, bardak kaldırma gibi yavaş hareketler */
    for (int k = 0; k < len; k++) {
        int i = start + k;
        if (i < 0 || i >= s->n) {
            continue;
        }
        double t = (double)k / SIG_HZ;
        s->ax[i] += 0.25 * sin(2 * M_PI * 0.9 * t);
        s->ay[i] += 0.20 * sin(2 * M_PI * 1.3 * t + 1.0);
        s->az[i] += 0.25 * sin(2 * M_PI * 0.6 * t + 2.0);
        s->gx[i] += 40.0 * sin(2 * M_PI * 0.7 * t + 0.5);
        s->gy[i] += 50.0 * sin(2 * M_PI * 0.4 * t);
        s->gz[i] += 30.0 * sin(2 * M_PI * 1.1 * t + 1.5);
    }
}

static int16_t clamp16(double v)
{
    if (v > 32767.0) {
        return 32767;
    }
    if (v < -32768.0) {
        return -32768;
    }
    return (int16_t)lround(v);
}

static double noise(signals_t *s)
{
    if (s->noise_lsb == 0) {
        return 0.0;
    }
    s->seed = s->seed * 1103515245u + 12345u; /* basit, platformdan bağımsız üreteç */
    int r = (int)((s->seed >> 16) % (unsigned)(2 * s->noise_lsb + 1));
    return (double)(r - s->noise_lsb);
}

void sig_end(signals_t *s)
{
    double theta = 0.0; /* Y ekseni etrafındaki açı, radyan */
    const double dt = 1.0 / SIG_HZ;

    for (int i = 0; i < s->n; i++) {
        theta += s->gy[i] * (M_PI / 180.0) * dt;
        /* Yerçekimi sensör eksenlerinde: başta (0, 0, +1 g), Y dönüşüyle X-Z düzleminde döner */
        double grav_x = -sin(theta);
        double grav_z = cos(theta);

        s->buf[i].accel[AX_X] = clamp16((s->ax[i] + grav_x) * ACC_LSB_PER_G + noise(s));
        s->buf[i].accel[AX_Y] = clamp16(s->ay[i] * ACC_LSB_PER_G + noise(s));
        s->buf[i].accel[AX_Z] = clamp16((s->az[i] + grav_z) * ACC_LSB_PER_G + noise(s));
        s->buf[i].gyro[AX_X] = clamp16(s->gx[i] * GYRO_LSB_PER_DPS + noise(s));
        s->buf[i].gyro[AX_Y] = clamp16(s->gy[i] * GYRO_LSB_PER_DPS + noise(s));
        s->buf[i].gyro[AX_Z] = clamp16(s->gz[i] * GYRO_LSB_PER_DPS + noise(s));
        s->buf[i].temp = 0;
    }

    free(s->ax);
    free(s->ay);
    free(s->az);
    free(s->gx);
    free(s->gy);
    free(s->gz);
    s->ax = s->ay = s->az = s->gx = s->gy = s->gz = NULL;
}

void sig_write_csv(const imu_raw_t *buf, int n, const uint8_t *flags, FILE *fp)
{
    fprintf(fp, "seq,flags,ax,ay,az,gx,gy,gz\n");
    for (int i = 0; i < n; i++) {
        fprintf(fp, "%d,%d,%d,%d,%d,%d,%d,%d\n", i & 0xFF, flags ? flags[i] : 0,
                buf[i].accel[0], buf[i].accel[1], buf[i].accel[2], buf[i].gyro[0],
                buf[i].gyro[1], buf[i].gyro[2]);
    }
}
