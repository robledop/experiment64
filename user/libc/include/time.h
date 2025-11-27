#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct tm
{
    int tm_sec;   // seconds [0, 60]
    int tm_min;   // minutes [0, 59]
    int tm_hour;  // hour [0, 23]
    int tm_mday;  // day of month [1, 31]
    int tm_mon;   // month [0, 11]
    int tm_year;  // years since 1900
    int tm_wday;  // day of week [0, 6] Sunday = 0
    int tm_yday;  // day of year [0, 365]
    int tm_isdst; // daylight saving time flag
};

#define strftime e64_strftime

// Convert UNIX timestamp (seconds since 1970-01-01 UTC) to struct tm (UTC).
void unix_timestamp_to_tm(uint32_t timestamp, struct tm *out);

// Minimal strftime implementation supporting common specifiers (%Y, %m, %d, %H, %M, %S, %B, %b).
size_t e64_strftime(const char *format, const struct tm *tm, char *out, size_t max);
