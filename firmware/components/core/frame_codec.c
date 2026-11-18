#include "frame_codec.h"

#include <inttypes.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Little-endian yardımcıları                                          */
/* Bilgisayarın kendi bayt sırasından bağımsız: byte'lar elle yazılır.  */
/* ------------------------------------------------------------------ */

static void put_i16_le(uint8_t *p, int16_t v)
{
    uint16_t u = (uint16_t)v; /* iki tümleyen bit deseni korunur */
    p[0] = (uint8_t)(u & 0xFFu);
    p[1] = (uint8_t)(u >> 8);
}

static int16_t get_i16_le(const uint8_t *p)
{
    uint16_t u = (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
    return (int16_t)u;
}

/* ------------------------------------------------------------------ */
/* Çerçeve                                                             */
/* ------------------------------------------------------------------ */

uint8_t frame_checksum(const uint8_t buf[PD_FRAME_LEN])
{
    uint8_t x = 0;
    for (int i = 1; i <= 14; i++) {
        x ^= buf[i];
    }
    return x;
}

void frame_pack(const log_frame_t *f, uint8_t out[PD_FRAME_LEN])
{
    out[0] = PD_FRAME_SYNC;
    out[1] = f->seq;
    out[2] = (uint8_t)(f->flags & (uint8_t)~PD_FLAG_RESERVED_MASK);
    for (int i = 0; i < 3; i++) {
        put_i16_le(&out[3 + 2 * i], f->accel[i]); /* byte 3..8  */
        put_i16_le(&out[9 + 2 * i], f->gyro[i]);  /* byte 9..14 */
    }
    out[15] = frame_checksum(out);
}

frame_status_t frame_unpack(const uint8_t in[PD_FRAME_LEN], log_frame_t *f)
{
    if (in[0] != PD_FRAME_SYNC) {
        return FRAME_BAD_SYNC;
    }
    if (in[15] != frame_checksum(in)) {
        return FRAME_BAD_CHECKSUM;
    }
    f->seq = in[1];
    f->flags = in[2];
    for (int i = 0; i < 3; i++) {
        f->accel[i] = get_i16_le(&in[3 + 2 * i]);
        f->gyro[i] = get_i16_le(&in[9 + 2 * i]);
    }
    return FRAME_OK;
}

void frame_from_raw(const imu_raw_t *raw, uint8_t seq, uint8_t flags, log_frame_t *f)
{
    f->seq = seq;
    f->flags = flags;
    for (int i = 0; i < 3; i++) {
        f->accel[i] = raw->accel[i];
        f->gyro[i] = raw->gyro[i];
    }
}

/* ------------------------------------------------------------------ */
/* Ad tabloları                                                        */
/* ------------------------------------------------------------------ */

const char *gesture_name(gesture_t g)
{
    switch (g) {
    case GESTURE_WEB: return "WEB";
    case GESTURE_ICE: return "ICE";
    case GESTURE_FIRE: return "FIRE";
    default: return NULL;
    }
}

const char *sensitivity_name(sensitivity_t s)
{
    switch (s) {
    case SENS_LOW: return "LOW";
    case SENS_MED: return "MED";
    case SENS_HIGH: return "HIGH";
    default: return NULL;
    }
}

const char *fault_name(fault_t f)
{
    switch (f) {
    case FAULT_I2C: return "I2C";
    case FAULT_WHO_AM_I: return "WHOAMI";
    case FAULT_SENSOR_STUCK: return "STUCK";
    case FAULT_LOOP_OVERRUN: return "OVERRUN";
    case FAULT_NVS: return "NVS";
    default: return NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Metin satırları                                                     */
/* ------------------------------------------------------------------ */

/* snprintf sonucunu ortak kurala çevirir: sığmadıysa ya da çok uzunsa -1 */
static int finish(int n, size_t size)
{
    if (n < 0 || (size_t)n >= size || n > PD_LINE_MAX) {
        return -1;
    }
    return n;
}

int line_boot(char *buf, size_t size, const char *fw, const char *reset, int imu_id)
{
    if (buf == NULL || fw == NULL || reset == NULL) {
        return -1;
    }
    int n;
    if (imu_id < 0) {
        n = snprintf(buf, size, "BOOT,%s,%s,NONE\n", fw, reset);
    } else {
        n = snprintf(buf, size, "BOOT,%s,%s,0x%02X\n", fw, reset, (unsigned)(imu_id & 0xFF));
    }
    return finish(n, size);
}

int line_evt(char *buf, size_t size, const gesture_result_t *r)
{
    if (buf == NULL || r == NULL) {
        return -1;
    }
    const char *name = gesture_name(r->gesture);
    if (name == NULL) {
        return -1;
    }
    uint32_t t_ms = (uint32_t)(r->t * (uint32_t)PD_TICK_MS); /* ~49 günde başa döner */
    int n = snprintf(buf, size, "EVT,%s,%" PRId32 ",%" PRIu32 ",%d\n", name, r->peak_mg, t_ms,
                     (int)r->margin_pct);
    return finish(n, size);
}

int line_cfg(char *buf, size_t size, sensitivity_t s)
{
    const char *name = sensitivity_name(s);
    if (buf == NULL || name == NULL) {
        return -1;
    }
    return finish(snprintf(buf, size, "CFG,%s\n", name), size);
}

int line_cal(char *buf, size_t size, bool ok, const int16_t bias[3])
{
    if (buf == NULL || bias == NULL) {
        return -1;
    }
    int n = snprintf(buf, size, "CAL,%s,%d,%d,%d\n", ok ? "OK" : "FAIL", (int)bias[0],
                     (int)bias[1], (int)bias[2]);
    return finish(n, size);
}

int line_err(char *buf, size_t size, fault_t f)
{
    const char *name = fault_name(f);
    if (buf == NULL || name == NULL) {
        return -1;
    }
    return finish(snprintf(buf, size, "ERR,%s\n", name), size);
}

int line_stat(char *buf, size_t size, uint32_t loop_max_us, uint32_t overruns,
              uint32_t i2c_errors)
{
    if (buf == NULL) {
        return -1;
    }
    int n = snprintf(buf, size, "STAT,%" PRIu32 ",%" PRIu32 ",%" PRIu32 "\n", loop_max_us,
                     overruns, i2c_errors);
    return finish(n, size);
}
