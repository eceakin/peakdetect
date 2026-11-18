/*
 * replay.h - kayıtlı (ya da yapay) IMU verisini firmware'in core zincirinden geçirir
 *
 *   CSV satırı -> imu_raw_t -> imu_convert -> gravity_filter -> gesture_detector
 *
 * Kartta çalışan kodun AYNISI (aynı .c dosyaları) PC'de çalışır. Böylece Adım 8'de
 * eşik değişiklikleri karta yüklenmeden önce bütün kayıtlarda denenir.
 *
 * Girdi CSV (decode.py çıktısı): ilk satır başlık. Sütun sırası serbest; gerekli
 * sütunlar ax, ay, az, gx, gy, gz (ham LSB). İsteğe bağlı: flags (bit 2 = buton
 * işareti). Diğer sütunlar yok sayılır. Her veri satırı bir örnektir (5 ms).
 *
 * Çıktı CSV:
 *   normal :  t_ms,gesture,margin_pct
 *             (yalnızca kabul edilen hareketler)
 *   --all  :  t_ms,result,reason,margin_pct,peak_mg,ax_pos,ax_abs,az_pos,az_abs,
 *             wy_abs,since_marker_ms
 *             (açılan her pencere; tepe değerler analyze.py için;
 *              since_marker_ms: son buton işaretinden bu yana geçen süre, yoksa -1)
 */
#ifndef REPLAY_H
#define REPLAY_H

#include <stdbool.h>
#include <stdio.h>

#include "pd_types.h"

typedef struct {
    sensitivity_t sens;
    calib_t calib;    /* ham kayıtlar bias'sız gelir; gerekirse CAL satırındaki değerler */
    bool all_windows; /* --all */
} replay_opts_t;

typedef struct {
    long frames;      /* işlenen örnek */
    long bad_lines;   /* okunamayan satır (atlanır, zaman yine ilerler) */
    long windows;     /* açılan pencere */
    long events[4];   /* gesture_t ile indekslenir: [1] WEB, [2] ICE, [3] FIRE */
    long rejects[5];  /* reject_reason_t ile indekslenir */
} replay_stats_t;

void replay_default_opts(replay_opts_t *o);

/* 0: tamam, -1: başlık okunamadı ya da gerekli sütun yok */
int replay_run(FILE *in, FILE *out, const replay_opts_t *o, replay_stats_t *st);

const char *replay_reason_name(reject_reason_t r);

#endif /* REPLAY_H */
