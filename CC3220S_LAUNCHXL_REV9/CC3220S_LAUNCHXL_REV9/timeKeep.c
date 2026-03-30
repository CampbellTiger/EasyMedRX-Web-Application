/*
 *  ======== time.c ========
 */
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <ti/net/slnetsock.h>
#include <ti/net/sntp/sntp.h>
#include <ti/display/Display.h>
#include "timeKeep.h"

#define NTP_TO_UNIX_OFFSET  (2208988800ULL)

static volatile uint32_t g_unixTime  = 0;
static volatile uint8_t  g_timeValid = 0;
static pthread_mutex_t   g_timeMutex;

extern Display_Handle display;

/* -----------------------------------------------------------------------
 *  Date calculation helpers
 * -------------------------------------------------------------------- */
static const uint8_t daysInMonth[] =
    { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static uint8_t isLeapYear(uint16_t year)
{
    return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
}

void unixToDateTime(uint32_t unix, DateTime_t *dt)
{
    dt->second = unix % 60; unix /= 60;
    dt->minute = unix % 60; unix /= 60;
    dt->hour   = unix % 24; unix /= 24;

    uint32_t days = unix;
    uint16_t year = 1970;

    while (1)
    {
        uint16_t daysInYear = isLeapYear(year) ? 366 : 365;
        if (days < daysInYear) break;
        days -= daysInYear;
        year++;
    }
    dt->year = year;

    uint8_t month = 0;
    while (1)
    {
        uint8_t dim = daysInMonth[month];
        if (month == 1 && isLeapYear(year)) dim = 29;
        if (days < dim) break;
        days -= dim;
        month++;
    }
    dt->month = month + 1;
    dt->day   = (uint8_t)days + 1;
}

uint32_t dateTimeToUnix(const DateTime_t *dt)
{
    uint32_t days = 0;
    uint16_t y;

    for (y = 1970; y < dt->year; y++)
    {
        days += isLeapYear(y) ? 366 : 365;
    }

    uint8_t m;
    for (m = 0; m < dt->month - 1; m++)
    {
        days += daysInMonth[m];
        if (m == 1 && isLeapYear(dt->year)) days++;
    }

    days += dt->day - 1;

    return (days * 86400)
         + (dt->hour   * 3600)
         + (dt->minute * 60)
         +  dt->second;
}

DateTime_t futureDate(uint32_t currentUnix,
                      uint16_t addYears,
                      uint8_t  addMonths,
                      uint32_t addDays,
                      uint32_t addSeconds)
{
    DateTime_t dt;
    unixToDateTime(currentUnix, &dt);

    /* Add months, carry overflow into years */
    dt.month += addMonths;
    while (dt.month > 12)
    {
        dt.month -= 12;
        dt.year++;
    }

    dt.year += addYears;

    /* Convert back to unix, then add raw days and seconds */
    uint32_t unix = dateTimeToUnix(&dt);
    unix += (addDays * 86400) + addSeconds;

    unixToDateTime(unix, &dt);
    return dt;
}

/* -----------------------------------------------------------------------
 *  Internal helpers
 * -------------------------------------------------------------------- */
void TimeKeeper_setTime(uint32_t unixTimestamp)
{
    pthread_mutex_lock(&g_timeMutex);
    g_unixTime  = unixTimestamp;
    g_timeValid = 1;
    pthread_mutex_unlock(&g_timeMutex);
}

uint32_t TimeKeeper_getTime(void)
{
    uint32_t t;
    pthread_mutex_lock(&g_timeMutex);
    t = g_unixTime;
    pthread_mutex_unlock(&g_timeMutex);
    return t;
}

uint8_t TimeKeeper_isValid(void)
{
    return g_timeValid;
}

/* -----------------------------------------------------------------------
 *  SNTP sync — call once Wi-Fi is up, then re-syncs every hour
 * -------------------------------------------------------------------- */
static int syncSNTP(void)
{
    uint64_t            ntpTime = 0;
    int32_t             status;
    SlNetSock_Timeval_t timeout;

    timeout.tv_sec  = 5;
    timeout.tv_usec = 0;

    const char *servers[] = { "pool.ntp.org", "time.google.com" };

    status = SNTP_getTime(servers, 2, &timeout, &ntpTime);
    if (status != 0)
    {
        Display_printf(display, 0, 0, "[TIME] SNTP sync failed, code: %d\r\n", status);
        return status;
    }

    uint32_t unixTime = (uint32_t)(ntpTime - NTP_TO_UNIX_OFFSET);
    TimeKeeper_setTime(unixTime);

    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)unixTime);
    Display_printf(display, 0, 0, "[TIME] SNTP sync OK, unix=%s\r\n", buf);

    return 0;
}

/* -----------------------------------------------------------------------
 *  Timekeeping thread
 *  - Tries SNTP on startup, retries every 30s until first sync succeeds
 *  - After first sync, ticks every second and re-syncs every hour
 * -------------------------------------------------------------------- */
void *TimeKeeper_thread(void *arg)
{
    pthread_mutex_init(&g_timeMutex, NULL);

    /* Retry until first SNTP sync succeeds */
    while (syncSNTP() != 0)
    {
        Display_printf(display, 0, 0, "[TIME] Retrying SNTP in 30s...\r\n");
        sleep(30);
    }

    uint32_t ticksSinceSync = 0;

    while (1)
    {
        sleep(1);

        pthread_mutex_lock(&g_timeMutex);
        g_unixTime++;
        pthread_mutex_unlock(&g_timeMutex);

        /* Re-sync every hour (3600 ticks) */
        ticksSinceSync++;
        if (ticksSinceSync >= 3600)
        {
            ticksSinceSync = 0;
            syncSNTP();
        }
    }

    return NULL;
}

/* -----------------------------------------------------------------------
 *  Spawn the timekeeping thread — call from SimpleLinkNetAppEventHandler
 *  once IP is acquired
 * -------------------------------------------------------------------- */
void TimeKeeper_start(void)
{
    pthread_t          thread;
    pthread_attr_t     attrs;
    struct sched_param priParam;

    pthread_attr_init(&attrs);
    priParam.sched_priority = 1;
    pthread_attr_setschedparam(&attrs, &priParam);
    pthread_attr_setstacksize(&attrs, 1024);

    pthread_create(&thread, &attrs, TimeKeeper_thread, NULL);
    pthread_attr_destroy(&attrs);
}

