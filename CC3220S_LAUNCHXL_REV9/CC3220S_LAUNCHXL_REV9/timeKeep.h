/*
 *  ======== time.h ========
 */
#ifndef TIMEKEEP_H
#define TIMEKEEP_H

#include <stdint.h>

/* -----------------------------------------------------------------------
 *  DateTime structure
 * -------------------------------------------------------------------- */
typedef struct
{
    uint16_t year;    /* e.g. 2025          */
    uint8_t  month;   /* 1-12               */
    uint8_t  day;     /* 1-31               */
    uint8_t  hour;    /* 0-23               */
    uint8_t  minute;  /* 0-59               */
    uint8_t  second;  /* 0-59               */
} DateTime_t;

/* -----------------------------------------------------------------------
 *  Timekeeping API
 * -------------------------------------------------------------------- */
void        TimeKeeper_start(void);
void        TimeKeeper_setTime(uint32_t unixTimestamp);
uint32_t    TimeKeeper_getTime(void);
uint8_t     TimeKeeper_isValid(void);

/* -----------------------------------------------------------------------
 *  Date calculation API
 * -------------------------------------------------------------------- */
void        unixToDateTime(uint32_t unix, DateTime_t *dt);
uint32_t    dateTimeToUnix(const DateTime_t *dt);
DateTime_t  futureDate(uint32_t currentUnix,
                       uint16_t addYears,
                       uint8_t  addMonths,
                       uint32_t addDays,
                       uint32_t addSeconds);

#endif /* TIMEKEP_H */
