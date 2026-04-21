/*
 *  ======== time.c ========
 */
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <ti/net/slnetsock.h>
#include <ti/net/sntp/sntp.h>
#include <ti/display/Display.h>
#include <semaphore.h>
#include <string.h>
#include "timeKeep.h"
#include "LCD_Menu.h"   /* drawClockFooter() */

extern volatile uint8_t sessionActive;   /* defined in json_make.c */

#define NTP_TO_UNIX_OFFSET  (2208988800ULL)

/* Hardware-absolute clock:
 * g_ntpBase        -- NTP unix time recorded at last SNTP sync.
 * g_realtimeAtSync -- CLOCK_REALTIME tv_sec recorded at the same instant.
 *
 * TimeKeeper_getTime() returns:
 *   g_ntpBase + (clock_gettime(CLOCK_REALTIME).tv_sec - g_realtimeAtSync)
 *
 * This is immune to scheduler starvation -- elapsed time is derived from
 * the hardware timer, not from software ticks.  sl_Task at priority 9
 * can starve this thread without affecting clock accuracy.               */
static volatile uint32_t g_ntpBase        = 0;
static volatile uint32_t g_realtimeAtSync = 0;
static volatile uint8_t  g_timeValid      = 0;
static pthread_mutex_t   g_timeMutex;

extern Display_Handle  display;
extern sem_t           sem;

/* Shared clock buffer declared in LCD_Menu.c */
extern volatile uint8_t g_clockDirty;
extern char             g_clockDate[12];
extern char             g_clockTime[12];

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
    Display_printf(display, 0, 0, "[ENTER] dateTimeToUnix\n\r");
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
    Display_printf(display, 0, 0, "[ENTER] futureDate\n\r");
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

/* Record the NTP unix time and the hardware CLOCK_REALTIME anchor
 * atomically.  Called by syncSNTP() after a successful SNTP response. */
void TimeKeeper_setTime(uint32_t unixTimestamp)
{
    Display_printf(display, 0, 0, "[ENTER] TimeKeeper_setTime\n\r");
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    pthread_mutex_lock(&g_timeMutex);
    g_ntpBase        = unixTimestamp;
    g_realtimeAtSync = (uint32_t)now.tv_sec;
    g_timeValid      = 1;
    pthread_mutex_unlock(&g_timeMutex);
}

/* Derive current time from the hardware clock -- immune to starvation. */
uint32_t TimeKeeper_getTime(void)
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    pthread_mutex_lock(&g_timeMutex);
    uint32_t base   = g_ntpBase;
    uint32_t anchor = g_realtimeAtSync;
    pthread_mutex_unlock(&g_timeMutex);

    return base + ((uint32_t)now.tv_sec - anchor);
}

uint32_t TimeKeeper_getLocalTime(void)
{
    return TimeKeeper_getTime() - TZ_OFFSET_SECONDS;
}

uint8_t TimeKeeper_isValid(void)
{
    return g_timeValid;
}

/* -----------------------------------------------------------------------
 *  SNTP sync  call once Wi-Fi is up, then re-syncs every hour
 * -------------------------------------------------------------------- */
static int syncSNTP(void)
{
    Display_printf(display, 0, 0, "[ENTER] syncSNTP\n\r");
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

    uint32_t unixTime = (uint32_t)((ntpTime >> 32) - NTP_TO_UNIX_OFFSET);
    TimeKeeper_setTime(unixTime);   /* records NTP base + hardware anchor */

    /* Print local time in MM/DD/YYYY HH:MM:SS AM/PM format */
    uint32_t localTime = unixTime - TZ_OFFSET_SECONDS;
    DateTime_t dt;
    unixToDateTime(localTime, &dt);
    uint8_t  hour12  = dt.hour % 12;
    if (hour12 == 0) hour12 = 12;
    const char *ampm = (dt.hour < 12) ? "AM" : "PM";
    char timeBuf[32];
    snprintf(timeBuf, sizeof(timeBuf),
             "%02u/%02u/%04u %02u:%02u:%02u %s",
             (unsigned)dt.month, (unsigned)dt.day, (unsigned)dt.year,
             (unsigned)hour12,   (unsigned)dt.minute, (unsigned)dt.second,
             ampm);
    Display_printf(display, 0, 0, "[TIME] %s\r\n", timeBuf);

    return 0;
}

/* -----------------------------------------------------------------------
 *  Timekeeping thread
 *  - Tries SNTP on startup, retries every 15s until first sync succeeds
 *  - After first sync, updates the LCD clock display every second and
 *    re-syncs SNTP every hour using hardware elapsed time (not tick count)
 *
 *  Clock accuracy is hardware-derived: getTime() reads CLOCK_REALTIME and
 *  computes (g_ntpBase + elapsed) regardless of how often this thread runs.
 *  Scheduler starvation by sl_Task delays display updates but does not
 *  cause time drift.
 * -------------------------------------------------------------------- */
void *TimeKeeper_thread(void *arg)
{
    Display_printf(display, 0, 0, "[ENTER] TimeKeeper_thread\n\r");

    /* Retry until first SNTP sync succeeds */
    while (syncSNTP() != 0)
    {
        Display_printf(display, 0, 0, "[TIME] Retrying SNTP in 15s...\r\n");
        sleep(15);
    }

    /* Record hardware time of first sync for re-sync interval tracking */
    struct timespec tsNow;
    clock_gettime(CLOCK_REALTIME, &tsNow);
    uint32_t lastSyncRealtime = (uint32_t)tsNow.tv_sec;

    while (1)
    {
        sleep(1);

        /* Derive current time from hardware -- no tick increment needed */
        uint32_t localNow = TimeKeeper_getLocalTime();
        DateTime_t dt;
        unixToDateTime(localNow, &dt);
        uint8_t hour12 = dt.hour % 12;
        if (hour12 == 0) hour12 = 12;
        const char *ampm = (dt.hour < 12) ? "AM" : "PM";
        char dateBuf[12], timeBuf[12];
        snprintf(dateBuf, sizeof(dateBuf), "%02u/%02u/%04u",
                 (unsigned)dt.month, (unsigned)dt.day, (unsigned)dt.year);
        snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u:%02u %s",
                 (unsigned)hour12, (unsigned)dt.minute,
                 (unsigned)dt.second, ampm);
        /* Write into shared buffer -- trackADC draws on its next tick.
         * No mutex needed: volatile flag + single-byte write is atomic
         * on Cortex-M4. Worst case: clock display is one tick stale.   */
        memcpy(g_clockDate, dateBuf, sizeof(g_clockDate));
        memcpy(g_clockTime, timeBuf, sizeof(g_clockTime));
        g_clockDirty = 1;

        /* During an active session the trackADC loop owns the LCD but does
         * not draw the clock -- update the footer directly here every second
         * so the clock stays live on the menu screen.                    */
        if (sessionActive)
            drawClockFooter(dateBuf, timeBuf);

        /* Re-sync every hour using hardware elapsed time -- not tick count.
         * Immune to starvation: even if sleep(1) fires late, the elapsed
         * calculation is correct because it reads CLOCK_REALTIME directly. */
        clock_gettime(CLOCK_REALTIME, &tsNow);
        if ((uint32_t)tsNow.tv_sec - lastSyncRealtime >= 3600)
        {
            lastSyncRealtime = (uint32_t)tsNow.tv_sec;
            syncSNTP();
        }
    }

    return NULL;
}

/* -----------------------------------------------------------------------
 *  Spawn the timekeeping thread  call from SimpleLinkNetAppEventHandler
 *  once IP is acquired
 * -------------------------------------------------------------------- */
void TimeKeeper_start(void)
{
    Display_printf(display, 0, 0, "[ENTER] TimeKeeper_start\n\r");
    pthread_t          thread;
    pthread_attr_t     attrs;
    struct sched_param priParam;

    /* Init mutex before the thread can call setTime/getTime */
    pthread_mutex_init(&g_timeMutex, NULL);

    pthread_attr_init(&attrs);
    priParam.sched_priority = 3;
    pthread_attr_setschedparam(&attrs, &priParam);
    pthread_attr_setstacksize(&attrs, 2048);

    pthread_create(&thread, &attrs, TimeKeeper_thread, NULL);
    pthread_attr_destroy(&attrs);
}
