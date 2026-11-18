/*
 * frame_codec.h - UART protokolünün C tarafı (docs/protocol.md ile birebir)
 *
 * 1) 16 byte'lık ham veri çerçevesi (LOG modu): paketleme / çözme
 * 2) ASCII metin satırları: BOOT, EVT, CFG, CAL, ERR, STAT
 *
 * Donanıma dokunmaz: çıktı bir tampona yazılır, gönderme işini
 * uart_link sürücüsü yapar. Bu yüzden PC'de test edilebilir.
 */
#ifndef FRAME_CODEC_H
#define FRAME_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_config.h"
#include "pd_types.h"

/* ------------------------------------------------------------------ */
/* 1) Ham veri çerçevesi                                               */
/* ------------------------------------------------------------------ */

/* flags byte'ının bitleri (protocol.md, "flags bitleri") */
#define PD_FLAG_DECISION_MASK 0x03u /* bit 1:0 = gesture_t */
#define PD_FLAG_BUTTON 0x04u        /* bit 2: buton işareti */
#define PD_FLAG_SATURATED 0x08u     /* bit 3: doymuş örnek */
#define PD_FLAG_FROZEN 0x10u        /* bit 4: yerçekimi tahmini donuk */
#define PD_FLAG_RESERVED_MASK 0xE0u /* bit 7:5: her zaman 0 */

/* Çerçevenin içeriği (sync ve checksum hariç) */
typedef struct {
    uint8_t seq;      /* 0..255, her çerçevede +1 */
    uint8_t flags;    /* PD_FLAG_* bitleri */
    int16_t accel[3]; /* LSB, ham */
    int16_t gyro[3];  /* LSB, ham (bias çıkarılmamış) */
} log_frame_t;

typedef enum {
    FRAME_OK = 0,
    FRAME_BAD_SYNC,     /* byte 0 != 0xA5 */
    FRAME_BAD_CHECKSUM, /* byte 15 != XOR(byte 1..14) */
} frame_status_t;

/* Byte 1..14'ün XOR'u */
uint8_t frame_checksum(const uint8_t buf[PD_FRAME_LEN]);

/* f -> 16 byte. Ayrılmış flag bitleri (7:5) sıfırlanarak yazılır. */
void frame_pack(const log_frame_t *f, uint8_t out[PD_FRAME_LEN]);

/* 16 byte -> f. FRAME_OK değilse f'ye dokunulmaz. */
frame_status_t frame_unpack(const uint8_t in[PD_FRAME_LEN], log_frame_t *f);

/* Yardımcı: ham sensör örneğinden çerçeve içeriği (sıcaklık gönderilmez) */
void frame_from_raw(const imu_raw_t *raw, uint8_t seq, uint8_t flags, log_frame_t *f);

/* ------------------------------------------------------------------ */
/* 2) Metin satırları                                                  */
/* ------------------------------------------------------------------ */
/*
 * Ortak kurallar:
 *  - buf'a NUL ile biten bir satır yazılır; satır '\n' ile biter.
 *  - Dönüş: yazılan karakter sayısı ('\n' dahil, NUL hariç).
 *  - Hata durumunda -1 döner: geçersiz girdi, tampon küçük, ya da
 *    satır PD_LINE_MAX'tan uzun. -1 döndüğünde buf'ın içeriğine güvenmeyin.
 *  - Önerilen tampon: char buf[PD_LINE_MAX + 1];
 */

/* BOOT,<fw>,<reset>,<imu_id>   imu_id < 0 ise "NONE" yazılır, değilse 0xHH */
int line_boot(char *buf, size_t size, const char *fw, const char *reset, int imu_id);

/* EVT,<gesture>,<peak_mg>,<t_ms>,<margin_pct>   r->gesture NONE ise -1 */
int line_evt(char *buf, size_t size, const gesture_result_t *r);

/* CFG,<LOW|MED|HIGH> */
int line_cfg(char *buf, size_t size, sensitivity_t s);

/* CAL,<OK|FAIL>,<bx>,<by>,<bz>   bias ham LSB */
int line_cal(char *buf, size_t size, bool ok, const int16_t bias[3]);

/* ERR,<fault>   FAULT_NONE ise -1 */
int line_err(char *buf, size_t size, fault_t f);

/* STAT,<loop_max_us>,<overruns>,<i2c_errors> */
int line_stat(char *buf, size_t size, uint32_t loop_max_us, uint32_t overruns,
              uint32_t i2c_errors);

/* Ad tabloları (geçersiz değerde NULL döner) */
const char *gesture_name(gesture_t g);
const char *sensitivity_name(sensitivity_t s);
const char *fault_name(fault_t f);

#endif /* FRAME_CODEC_H */
