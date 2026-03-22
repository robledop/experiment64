#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

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

// Convert UNIX timestamp (seconds since 1970-01-01 UTC) to struct tm (UTC).
void unix_timestamp_to_tm(uint32_t timestamp, struct tm *out);
struct tm *localtime_r(const time_t *clock, struct tm *result);
struct tm *localtime(const time_t *clock);
time_t mktime(struct tm *tm);
char *asctime(const struct tm *tm);
char *asctime_r(const struct tm *tm, char *buf);
char *ctime(const time_t *clock);
char *ctime_r(const time_t *clock, char *buf);
size_t strftime(char *s, size_t max, const char *format, const struct tm *tm);
size_t e64_strftime(const char *format, const struct tm *tm, char *out, size_t max);
time_t time(long long int *time);
uint64_t now_ms(void);
