#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>

static bool is_leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static const int month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static const char *month_names[] = {"January", "February", "March", "April", "May", "June",
                                    "July", "August", "September", "October", "November", "December"};
static const char *month_names_short[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static const char *day_names_short[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

void unix_timestamp_to_tm(uint32_t timestamp, struct tm *out)
{
    if (!out)
        return;

    uint32_t seconds = timestamp;
    out->tm_sec = (int)(seconds % 60);
    seconds /= 60;
    out->tm_min = (int)(seconds % 60);
    seconds /= 60;
    out->tm_hour = (int)(seconds % 24);
    uint32_t days = seconds / 24;

    // 1970-01-01 was a Thursday (4)
    out->tm_wday = (int)((days + 4) % 7);

    int year = 1970;
    while (true)
    {
        uint32_t days_in_year = is_leap(year) ? 366 : 365;
        if (days < days_in_year)
            break;
        days -= days_in_year;
        year++;
    }
    out->tm_year = year - 1900;
    out->tm_yday = (int)days;

    int month = 0;
    while (month < 12)
    {
        int dim = month_days[month];
        if (month == 1 && is_leap(year))
            dim++;
        if (days < (uint32_t)dim)
            break;
        days -= dim;
        month++;
    }
    out->tm_mon = month;
    out->tm_mday = (int)days + 1;
    out->tm_isdst = 0;
}

struct tm *localtime_r(const time_t *clock, struct tm *result)
{
    if (!clock || !result)
        return nullptr;
    unix_timestamp_to_tm((uint32_t)*clock, result);
    return result;
}

static struct tm localtime_buf;

struct tm *localtime(const time_t *clock)
{
    if (!clock)
        return nullptr;
    return localtime_r(clock, &localtime_buf);
}

time_t mktime(struct tm *tm)
{
    (void)tm;
    return (time_t)-1;
}

size_t strftime(char *s, size_t max, const char *format, const struct tm *tm)
{
    return e64_strftime(format, tm, s, max);
}

static void append_str(char **out, size_t *remaining, const char *s)
{
    while (*s && *remaining > 1)
    {
        **out = *s;
        (*out)++;
        (*remaining)--;
        s++;
    }
}

static void append_int_padded(char **out, size_t *remaining, int value, int width)
{
    char buf[16];
    int idx = 0;
    if (value == 0)
        buf[idx++] = '0';
    else
    {
        int v = value;
        if (v < 0)
            v = -v;
        while (v > 0 && idx < (int)sizeof(buf))
        {
            buf[idx++] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    while (idx < width && idx < (int)sizeof(buf))
        buf[idx++] = '0';
    while (idx-- > 0 && *remaining > 1)
    {
        **out = buf[idx];
        (*out)++;
        (*remaining)--;
    }
}

size_t e64_strftime(const char *format, const struct tm *tm, char *out, size_t max)
{
    if (!out || max == 0)
        return 0;
    char *p = out;
    size_t remaining = max;

    for (const char *f = format; *f && remaining > 1; f++)
    {
        if (*f != '%')
        {
            *p++ = *f;
            remaining--;
            continue;
        }
        f++;
        switch (*f)
        {
        case 'Y':
            append_int_padded(&p, &remaining, tm->tm_year + 1900, 4);
            break;
        case 'm':
            append_int_padded(&p, &remaining, tm->tm_mon + 1, 2);
            break;
        case 'd':
            append_int_padded(&p, &remaining, tm->tm_mday, 2);
            break;
        case 'H':
            append_int_padded(&p, &remaining, tm->tm_hour, 2);
            break;
        case 'M':
            append_int_padded(&p, &remaining, tm->tm_min, 2);
            break;
        case 'S':
            append_int_padded(&p, &remaining, tm->tm_sec, 2);
            break;
        case 'B':
        {
            int mon_idx = tm->tm_mon % 12;
            if (mon_idx < 0)
                mon_idx += 12;
            append_str(&p, &remaining, month_names[mon_idx]);
            break;
        }
        case 'b':
        {
            int mon_idx = tm->tm_mon % 12;
            if (mon_idx < 0)
                mon_idx += 12;
            append_str(&p, &remaining, month_names_short[mon_idx]);
            break;
        }
        case '%':
            *p++ = '%';
            remaining--;
            break;
        default:
            // Unsupported specifier; copy literally
            *p++ = '%';
            if (remaining > 1)
            {
                remaining--;
                if (*f)
                {
                    *p++ = *f;
                    remaining--;
                }
            }
            break;
        }
    }

    *p = '\0';
    return (size_t)(p - out);
}

/// @brief Convert a broken-down time to a fixed-format string (reentrant).
/// @param tm Pointer to the broken-down time.
/// @param buf Caller-supplied buffer (at least 26 bytes).
/// @return Pointer to buf, or NULL on error.
char *asctime_r(const struct tm *tm, char *buf)
{
    if (!tm || !buf)
        return nullptr;

    const char *wday = day_names_short[tm->tm_wday % 7];
    const char *mon = month_names_short[tm->tm_mon % 12];

    // "Wed Jun 30 21:49:08 1993\n\0" — exactly 26 bytes
    char *p = buf;
    size_t remaining = 26;

    append_str(&p, &remaining, wday);
    append_str(&p, &remaining, " ");
    append_str(&p, &remaining, mon);
    append_str(&p, &remaining, " ");
    append_int_padded(&p, &remaining, tm->tm_mday, 2);
    append_str(&p, &remaining, " ");
    append_int_padded(&p, &remaining, tm->tm_hour, 2);
    append_str(&p, &remaining, ":");
    append_int_padded(&p, &remaining, tm->tm_min, 2);
    append_str(&p, &remaining, ":");
    append_int_padded(&p, &remaining, tm->tm_sec, 2);
    append_str(&p, &remaining, " ");
    append_int_padded(&p, &remaining, tm->tm_year + 1900, 4);
    append_str(&p, &remaining, "\n");
    *p = '\0';

    return buf;
}

static char asctime_buf[26];

/// @brief Convert a broken-down time to a fixed-format string.
/// @param tm Pointer to the broken-down time.
/// @return Pointer to a static buffer containing the formatted string.
char *asctime(const struct tm *tm)
{
    return asctime_r(tm, asctime_buf);
}

/// @brief Convert a time_t to a human-readable string (reentrant).
/// @param clock Pointer to the time value.
/// @param buf Caller-supplied buffer (at least 26 bytes).
/// @return Pointer to buf, or NULL on error.
char *ctime_r(const time_t *clock, char *buf)
{
    if (!clock || !buf)
        return nullptr;
    struct tm tm;
    localtime_r(clock, &tm);
    return asctime_r(&tm, buf);
}

static char ctime_buf[26];

/// @brief Convert a time_t to a human-readable string.
/// @param clock Pointer to the time value.
/// @return Pointer to a static buffer containing the formatted string.
char *ctime(const time_t *clock)
{
    return ctime_r(clock, ctime_buf);
}

time_t time(long long int *time)
{
    struct timeval tv = {0};
    if (gettimeofday(&tv, nullptr) < 0)
        return -1;

    if (time)
        *time = (long long int)tv.tv_sec;
    return (time_t)tv.tv_sec;
}

uint64_t now_ms(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, nullptr) != 0)
        return 0;
    return (uint64_t)tv.tv_sec * 1000ull + (uint64_t)(tv.tv_usec / 1000u);
}
