#define _POSIX_C_SOURCE 200809L
#include "odin_time.h"
#include <stdlib.h>

bool odin_time_configure(void)
{
    if (setenv("TZ", ODIN_MONTREAL_TZ, 1) != 0) return false;
    tzset();
    return true;
}
bool odin_time_format(time_t utc, char *text, size_t size)
{
    /* Do not display the boot-time 1970 date before network synchronization. */
    if (!text || !size) return false;
    text[0] = 0;
    if (utc < (time_t)1704067200) return false;
    struct tm local;
    if (!localtime_r(&utc, &local)) return false;
    return strftime(text, size, "Montreal, QC  |  %a %d %b %Y  %H:%M:%S %Z", &local) != 0;
}
