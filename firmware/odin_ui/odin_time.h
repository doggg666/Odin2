#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
/* Montreal: Eastern time, second Sunday in March / first Sunday in November. */
#define ODIN_MONTREAL_TZ "EST5EDT,M3.2.0/2,M11.1.0/2"
bool odin_time_configure(void);
bool odin_time_format(time_t utc, char *text, size_t size);
