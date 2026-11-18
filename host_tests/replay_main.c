/*
 * replay - komut satırı programı
 *
 * Kullanım:
 *   replay <girdi.csv> [--sens LOW|MED|HIGH] [--all] [--bias bx,by,bz] [-o cikti.csv]
 *
 * Örnekler (PowerShell, depo kökünden):
 *   .\build_host\replay.exe build_host\demo.csv
 *   .\build_host\replay.exe data\ece_fire_01.csv --sens HIGH
 *   .\build_host\replay.exe data\ece_fire_01.csv --all -o fire_windows.csv
 *
 * Sonuçlar stdout'a (ya da -o dosyasına), özet stderr'e yazılır.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "replay.h"

static void usage(void)
{
    fprintf(stderr, "kullanim: replay <girdi.csv> [--sens LOW|MED|HIGH] [--all] "
                    "[--bias bx,by,bz] [-o cikti.csv]\n");
}

int main(int argc, char **argv)
{
    replay_opts_t o;
    replay_default_opts(&o);
    const char *in_path = NULL, *out_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--all") == 0) {
            o.all_windows = true;
        } else if (strcmp(argv[i], "--sens") == 0 && i + 1 < argc) {
            const char *s = argv[++i];
            if (strcmp(s, "LOW") == 0) {
                o.sens = SENS_LOW;
            } else if (strcmp(s, "MED") == 0) {
                o.sens = SENS_MED;
            } else if (strcmp(s, "HIGH") == 0) {
                o.sens = SENS_HIGH;
            } else {
                usage();
                return 2;
            }
        } else if (strcmp(argv[i], "--bias") == 0 && i + 1 < argc) {
            int bx, by, bz;
            if (sscanf(argv[++i], "%d,%d,%d", &bx, &by, &bz) != 3) {
                usage();
                return 2;
            }
            o.calib.gyro_bias[0] = (int16_t)bx;
            o.calib.gyro_bias[1] = (int16_t)by;
            o.calib.gyro_bias[2] = (int16_t)bz;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (argv[i][0] != '-' && in_path == NULL) {
            in_path = argv[i];
        } else {
            usage();
            return 2;
        }
    }
    if (in_path == NULL) {
        usage();
        return 2;
    }

    FILE *in = fopen(in_path, "r");
    if (in == NULL) {
        perror(in_path);
        return 1;
    }
    FILE *out = out_path ? fopen(out_path, "w") : stdout;
    if (out == NULL) {
        perror(out_path);
        fclose(in);
        return 1;
    }

    replay_stats_t st;
    int rc = replay_run(in, out, &o, &st);
    fclose(in);
    if (out != stdout) {
        fclose(out);
    }
    if (rc != 0) {
        fprintf(stderr, "replay: %s okunamadi (baslik/sutunlar)\n", in_path);
        return 1;
    }

    fprintf(stderr,
            "ornek: %ld (%.1f s)  hatali satir: %ld  pencere: %ld\n"
            "WEB: %ld  ICE: %ld  FIRE: %ld\n"
            "ret -> NO_RULE: %ld  AMBIGUOUS: %ld  SATURATED: %ld  WEB_TOO_HARD: %ld\n",
            st.frames, st.frames / 200.0, st.bad_lines, st.windows, st.events[1], st.events[2],
            st.events[3], st.rejects[1], st.rejects[2], st.rejects[3], st.rejects[4]);
    return 0;
}
