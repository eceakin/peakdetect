#include <stdio.h>
#include "sdkconfig"

void app_main(void)
{
    printf("PeakDetect boot\n");
#ifdef CONFIG_PD_LOG_MODE_ENABLE
    printf("log mode: compiled in\n");
#endif
#ifdef CONFIG_PD_WIFI_ENABLE
    printf("wifi: compiled in\n");
#endif
}