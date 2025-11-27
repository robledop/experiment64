#include <time.h>
#include <string.h>

static bool is_leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static const int month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static const char *month_names[] = {"January", "February", "March", "April", "May", "June",
                                    "July", "August", "September", "October", "November", "December"};
static const char *month_names_short[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

void unix_timestamp_to_tm(uint32_t timestamp, struct tm *out)
{
    if (!out)
        return;

    uint32_t seconds = timestamp;
    out->tm_sec = seconds % 60;
    seconds /= 60;
    out->tm_min = seconds % 60;
    seconds /= 60;
    out->tm_hour = seconds % 24;
    uint32_t days = seconds / 24;

    // 1970-01-01 was a Thursday (4)
    out->tm_wday = (days + 4) % 7;

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
            append_str(&p, &remaining, month_names[tm->tm_mon % 12]);
            break;
        case 'b':
            append_str(&p, &remaining, month_names_short[tm->tm_mon % 12]);
            break;
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
