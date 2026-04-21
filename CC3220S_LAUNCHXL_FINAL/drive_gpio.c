#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <ti/drivers/ADC.h>
#include <ti/display/Display.h>
#include <ti/sysbios/BIOS.h>
#include <ti/drivers/GPIO.h>
#include <ti/drivers/SPI.h>
#include <ti/drivers/PWM.h>
#include "ti_drivers_config.h"
#include <semaphore.h>
#include <time.h>
#include "LCD_Menu.h"
#include "json_make.h"      /* getCompartStock(), decrementCompartStock(), activeFile */
#include "error_handler.h"  /* logError(), DispensingError */
#include "http_queue.h"
#include "timeKeep.h"       /* TimeKeeper_isValid(), TimeKeeper_getLocalTime() */
#include "drive_gpio.h"

/*Mutex to control access to SPI line*/
pthread_mutex_t g_spiMutex;

/*Note: Consider adding delays if problem persistswith LCD*/

SPI_Handle  spi;
SPI_Params  spiParams;
ADC_Handle  adc;
ADC_Params  adcParams;

static volatile int dispenseTracker = 0;
static sem_t dispenseSem;
static uint32_t currentSPIRate = 0;

void *RC522_senseRFID(void *pvParameters);

/* ---------------------------------------------------------------
 * Interrupt
 * --------------------------------------------------------------- */
void IRQHandler(uint_least8_t index)
{
    /* ISR: no Display_printf -- unsafe in interrupt context.
     * Only post if a dispense is actively in progress; ignores
     * noise / spurious edges before readIR() arms the tracker. */
    if (dispenseTracker > 0)
        sem_post(&dispenseSem);
}

void configInterrupt(void)
{
    Display_printf(display, 0, 0, "[ENTER] configInterrupt\n\r");
    sem_init(&dispenseSem, 0, 0);   /* init once -- ISR posts, readIR waits */
    GPIO_setConfig(CONFIG_GPIO_11, GPIO_CFG_IN_PU | GPIO_CFG_IN_INT_FALLING);
    GPIO_setCallback(CONFIG_GPIO_11, IRQHandler);
    GPIO_enableInt(CONFIG_GPIO_11);
}

void configSPI(void)
{
    Display_printf(display, 0, 0, "[ENTER] configSPI\n\r");
    SPI_Params_init(&spiParams);
    spiParams.mode        = SPI_CONTROLLER;
    spiParams.bitRate     = 4000000;   /* LCD default */
    spiParams.frameFormat = SPI_POL0_PHA0;
    spiParams.dataSize    = 8;
    spi = SPI_open(CONFIG_SPI_0, &spiParams);
    if (spi == NULL) {
        Display_printf(display, 0, 0, "configSPI: SPI_open failed\n\r");
        while(1);
    }
    currentSPIRate = 4000000;
}

 /* setSPIRate() -- closes and reopens SPI_0 at the requested rate.
  * No-op when the bus is already at that rate.
  * Must be called with g_spiMutex held.                           */
void setSPIRate(uint32_t hz)
{
    if (hz == currentSPIRate)
        return;
    if (spi != NULL) { SPI_close(spi); spi = NULL; }
    SPI_Params_init(&spiParams);
    spiParams.mode        = SPI_CONTROLLER;
    spiParams.bitRate     = hz;
    spiParams.frameFormat = SPI_POL0_PHA0;
    spiParams.dataSize    = 8;
    spi = SPI_open(CONFIG_SPI_0, &spiParams);
    if (spi == NULL) {
        Display_printf(display, 0, 0, "setSPIRate: SPI_open failed\n\r");
        while(1);
    }
    currentSPIRate = hz;
}

/*check if SPI is working correctly*/
void* testSPI(void* pvParameters)
{
    Display_printf(display, 0, 0, "[ENTER] testSPI\n\r");
    configSPI();
    SPI_Transaction t;
    uint8_t tx[2] = {0xFF, 0xAA};
    uint8_t rx[2];
    memset(&t, 0, sizeof(t));
    t.count = 2; t.txBuf = tx; t.rxBuf = rx;
    while(1)
    {
        GPIO_write(CONFIG_GPIO_30, 1);
        GPIO_write(CONFIG_GPIO_08, 0);
        GPIO_write(CONFIG_GPIO_09, 0);
        GPIO_toggle(CONFIG_GPIO_00);
        usleep(5);
        bool ok = SPI_transfer(spi, &t);
        usleep(5);
        GPIO_write(CONFIG_GPIO_08, 1);
        GPIO_write(CONFIG_GPIO_09, 1);
        if (!ok) { Display_printf(display, 0, 0, "SPI transfer failed"); return NULL; }
        Display_printf(display, 0, 0, "Tx[0]=%02x Tx[1]=%02x", tx[0], tx[1]);
        Display_printf(display, 0, 0, "Rx[0]=%02x Rx[1]=%02x", rx[0], rx[1]);
    }
    return NULL;
}

/* ---------------------------------------------------------------
 * RFID
 * --------------------------------------------------------------- */

/*Translated from Arduino*/
void RC522_Write(uint8_t reg, uint8_t value)
{
    pthread_mutex_lock(&g_spiMutex);
    SPI_Transaction t;
    uint8_t tx[2] = { (reg << 1) & 0x7E, value };
    uint8_t rx[2];
    memset(&t, 0, sizeof(t));
    t.count = 2; t.txBuf = tx; t.rxBuf = rx;
    GPIO_write(CONFIG_GPIO_08, 0);
    /*usleep(5);*/
    bool ok = SPI_transfer(spi, &t);
    /*usleep(5); */
    GPIO_write(CONFIG_GPIO_08, 1);
    /*usleep(5);*/
    pthread_mutex_unlock(&g_spiMutex);
    if (!ok) Display_printf(display, 0, 0, "SPI transfer failed");
}

uint8_t RC522_Read(uint8_t reg)
{
    pthread_mutex_lock(&g_spiMutex);
    SPI_Transaction t;
    uint8_t tx[2] = { ((reg << 1) & 0x7E) | 0x80, 0x00 };
    uint8_t rx[2] = {0xAB, 0xCD};
    memset(&t, 0, sizeof(t));
    t.count = 2; t.txBuf = tx; t.rxBuf = rx;
    GPIO_write(CONFIG_GPIO_08, 0);
    /*usleep(5);*/
    bool ok = SPI_transfer(spi, &t);
    /*usleep(5);*/
    GPIO_write(CONFIG_GPIO_08, 1);
    /*usleep(5);*/
    pthread_mutex_unlock(&g_spiMutex);
    uint8_t result = (ok ? rx[1] : 0xFF);
    return result;
}

/*Translated from Arduino*/
static void RC522_SetBitMask(uint8_t reg, uint8_t mask)
{
    RC522_Write(reg, RC522_Read(reg) | mask);
}

/*Translated from Arduino*/
static void RC522_AntennaOn(void)
{
    uint8_t v = RC522_Read(TxControlReg);
    if ((v & 0x03U) != 0x03U) RC522_Write(TxControlReg, v | 0x03U);
}

/*Translated from Arduino*/
int RC522_Init_CC3220(void)
{
    Display_printf(display, 0, 0, "[ENTER] RC522_Init_CC3220\n\r");
    /* RST pin is tied to 3.3V -- always held high, no GPIO control needed */
    GPIO_write(CONFIG_GPIO_08, 1);   /* deassert chip select */
    usleep(10000);
    RC522_Write(CommandReg, PCD_SoftReset);

    /* Poll for reset complete: CommandReg bit 4 (PowerDown) clears when
     * the RC522 oscillator is running.  On cold external power the 3.3V
     * rail may be slow to ramp; 50 ms was not always enough.
     * Spec max startup time is 37 ms; allow 100 ms total here.          */
    {
        int tout = 100;  /* 100 × 1 ms = 100 ms max */
        while (tout--)
        {
            usleep(1000);
            if ((RC522_Read(CommandReg) & 0x10) == 0)
                break;
        }
        if (tout <= 0)
            Display_printf(display, 0, 0,
                "RC522_Init: reset timeout -- oscillator may not be running\n\r");
    }

    uint8_t version = RC522_Read(VersionReg);
    Display_printf(display, 0, 0, "RC522 version: 0x%02x\n\r", version);
    /*if (version == 0x00 || version == 0xFF)
    {
        Display_printf(display, 0, 0,
            "RC522_Init: bad version (0x%02x) -- SPI or power fault\n\r", version);
        return -1;
    }*/

    RC522_Write(TModeReg,      0x80);
    RC522_Write(TPrescalerReg, 0xA9);
    RC522_Write(TReloadRegH,   0x03);
    RC522_Write(TReloadRegL,   0xE8);
    RC522_Write(TxASKReg,      0x40);
    RC522_Write(ModeReg,       0x3D);
    RC522_Write(RFCfgReg,      0x48);
    RC522_AntennaOn();
    setSPIRate(1000000);
    return 0;
}

/*Translated from Arduino*/
static RC522_Status RC522_Transceive(const uint8_t *sendData, uint8_t sendLen,
                                     uint8_t *backData, uint8_t *backLen,
                                     uint8_t validBitsTx, uint8_t *validBitsRx)
{
    RC522_Write(CommandReg,   PCD_Idle);
    RC522_Write(ComIrqReg,    0x7F);
    RC522_Write(FIFOLevelReg, 0x80);
    for (uint8_t i = 0; i < sendLen; i++) RC522_Write(FIFODataReg, sendData[i]);
    RC522_Write(BitFramingReg, validBitsTx & 0x07);
    RC522_Write(CommandReg,    PCD_Transceive);
    RC522_SetBitMask(BitFramingReg, 0x80);
    int timeout = 50;

    while (timeout--) {
        uint8_t irq = RC522_Read(ComIrqReg);
        if (irq & 0x30) break;
        if (irq & 0x01) return RC522_TIMEOUT;
        usleep(1000);
    }

    if (timeout <= 0)
        return RC522_TIMEOUT;
    uint8_t err = RC522_Read(ErrorReg);

    if (err & 0x13)
        return RC522_ERR;

    if (err & 0x08)
        return RC522_COLLISION;

    uint8_t n = RC522_Read(FIFOLevelReg);
    if (n > *backLen)
        return RC522_ERR;
    for (uint8_t i = 0; i < n; i++) backData[i] = RC522_Read(FIFODataReg);
    *backLen = n;
    if (validBitsRx) *validBitsRx = RC522_Read(ControlReg) & 0x07;
    return RC522_OK;
}

RC522_Status RC522_RequestA(uint8_t *atqa)
{
    uint8_t cmd = 0x26, len = 2, validBits = 0;
    RC522_Status st = RC522_Transceive(&cmd, 1, atqa, &len, 7, &validBits);
    if (st != RC522_OK) return st;
    if (len != 2 || validBits != 0) return RC522_ERR;
    return RC522_OK;
}

RC522_Status RC522_Anticoll_CL1(uint8_t uid[4], uint8_t *bcc)
{
    Display_printf(display, 0, 0, "[ENTER] RC522_Anticoll_CL1\n\r");
    uint8_t cmd[2] = {0x93, 0x20}, resp[5] = {0}, len = sizeof(resp);
    RC522_Status st = RC522_Transceive(cmd, 2, resp, &len, 0, NULL);
    if (st != RC522_OK || len != 5) return RC522_ERR;
    if ((resp[0]^resp[1]^resp[2]^resp[3]) != resp[4]) return RC522_ERR;
    uid[0]=resp[0]; uid[1]=resp[1]; uid[2]=resp[2]; uid[3]=resp[3];
    if (bcc) *bcc = resp[4];
    return RC522_OK;
}

/* ---------------------------------------------------------------
 * RFID session loop
 *
 * Top-level user session state machine:
 *
 *   SCANNING state  (sessionActive == 0)
 *     Poll RC522 every 50 ms. On a valid UID match, look up the
 *     json file, set activeFile, set sessionActive = 1, then
 *     launch bootLCD() which draws the menu and spawns trackADC().
 *
 *   SESSION state   (sessionActive == 1)
 *     Yield -- the LCD / trackADC threads own the device.
 *     Block-poll until cursorSelect() clears sessionActive to 0
 *     (Exit on MENU_MAIN).
 *
 *   LOGOUT          (sessionActive transitions to 0)
 *     Clear activeFile, redraw the idle scan screen, resume polling.
 * --------------------------------------------------------------- */
void *RC522_senseRFID(void *pvParameters)
{
    uint32_t prevNow  = 0;
    uint32_t localNow = 0;

    Display_printf(display, 0, 0, "[ENTER] RC522_senseRFID\n\r");
    if (RC522_Init_CC3220() != 0)
    {
        Display_printf(display, 0, 0, "RC522 init failed\n\r");
        return NULL;
    }

    uint8_t atqa[2], uid[4], bcc;
    char    uidStr[12];   /* "AABBCCDD\0" */

    uint8_t prevActive = 0;

    /* Note: idle screen is drawn by initLCD_hardware().
     * Do not touch the display here -- LCD may not be initialised yet. */

    while (1)
    {
        if (sessionActive == 0)
        {
            /* ---- SCANNING: poll RC522 every 50 ms ---- */
            pthread_mutex_lock(&g_spiMutex);
            setSPIRate(1000000);   /* setSPIRate must be called with g_spiMutex held */
            pthread_mutex_unlock(&g_spiMutex);
            if (RC522_RequestA(atqa) == RC522_OK &&
                RC522_Anticoll_CL1(uid, &bcc) == RC522_OK)
            {
                snprintf(uidStr, sizeof(uidStr), "%02x%02x%02x%02x",
                         uid[0], uid[1], uid[2], uid[3]);

                Display_printf(display, 0, 0,
                    "UID: %02x %02x %02x %02x\n\r",
                    uid[0], uid[1], uid[2], uid[3]);

                /* Card confirmed -- all remaining work this iteration is LCD.
                 * Switch to 4 MHz now; bootLCD inherits this rate.          */

            /* if(1){

                snprintf(uidStr, sizeof(uidStr),"87447d33");*/

                pthread_mutex_lock(&g_spiMutex);
                setSPIRate(4000000);
                pthread_mutex_unlock(&g_spiMutex);

                const char *found = uidToFilename(uidStr);
                if (found != NULL)
                {
                    /* activeFile set before sync so syncProfileFromServer has
                     * the filename; sessionActive stays 0 until sync completes
                     * so the LOGOUT check at the bottom of the loop cannot fire. */
                    activeFile = found;
                    Display_printf(display, 0, 0,
                        "activeFile set: %s\n\r", activeFile);

                    /* Show "Card accepted" briefly */
                    clearMenuArea();
                    drawTextbox(4, MENU_Y + (MENU_H / 2) - FONT_H,
                                LCD_W - 8, FONT_H * 2,
                                "Card accepted...", COL_FG, COL_BG);

                    /* Block this thread while we POST the stored profile and
                     * wait for the server's response (which carries live
                     * window0-3 fields).  The queue is always empty at this
                     * point (between sessions), so the request executes first.
                     * On failure the FS file is unchanged and local data is used.
                     * "Updating..." remains on screen during the round-trip.   */
                    clearMenuArea();
                    drawTextbox(4, MENU_Y + (MENU_H / 2) - FONT_H,
                                LCD_W - 8, FONT_H * 2,
                                "Updating...", COL_FG, COL_BG);

                    /* On failure the FS file is unchanged -- bootLCD calls
                     * openActiveProfile() on its 8192-byte stack, which is
                     * large enough for the JSON library.  Do not call it here
                     * on RC522_senseRFID's 4096-byte stack: the combined depth
                     * of httpQ_postGetResponse + syncProfileFromServer +
                     * openActiveProfile overflows it, corrupting lcdAttrs and
                     * causing pthread_create to spin.                         */
                    syncProfileFromServer(activeFile);

                    /* Transition to session -- bootLCD spawned below on 0→1 edge */
                    sessionActive = 1;
                }
                else if (isTrustedUID(uidStr))
                {
                    /* UID is in the trusted list but has no local profile
                     * slot yet -- request a full profile from the server.
                     * Block here (same as syncProfileFromServer) while the
                     * round-trip completes; bootLCD spawned below on success.  */
                    Display_printf(display, 0, 0,
                        "UID trusted, no local profile -- registering\n\r");

                    clearMenuArea();
                    drawTextbox(4, MENU_Y + (MENU_H / 2) - FONT_H,
                                LCD_W - 8, FONT_H * 2,
                                "Registering...", COL_FG, COL_BG);

                    const char *newSlot = requestNewProfile(uidStr);
                    if (newSlot != NULL)
                    {
                        activeFile    = newSlot;
                        /* Transition to session -- bootLCD spawned below on 0→1 edge */
                        sessionActive = 1;
                    }
                    else
                    {
                        /* Server rejected or returned no credentials */
                        clearMenuArea();
                        drawTextbox(4, MENU_Y + (MENU_H / 2) - FONT_H,
                                    LCD_W - 8, FONT_H * 2,
                                    "Registration failed", 0xF800, COL_BG);
                        usleep(2000000);
                        clearMenuArea();
                        drawTextbox(4, MENU_Y + (MENU_H / 2) - FONT_H,
                                    LCD_W - 8, FONT_H * 2,
                                    "Scan RFID card", COL_FG, COL_BG);
                    }
                }
                else
                {
                    /* UID not in the trusted list at all */
                    Display_printf(display, 0, 0, "UID not trusted\n\r");
                    clearMenuArea();
                    drawTextbox(4, MENU_Y + (MENU_H / 2) - FONT_H,
                                LCD_W - 8, FONT_H * 2,
                                "Card not recognised", 0xF800, COL_BG);
                    sleep(3);
                    clearMenuArea();
                    drawTextbox(4, MENU_Y + (MENU_H / 2) - FONT_H,
                                LCD_W - 8, FONT_H * 2,
                                "Scan RFID card", COL_FG, COL_BG);
                }
            }

            usleep(50000);

            /* ---- CLOCK FOOTER UPDATE ----
             * Only draw when:
             *   1. spi != NULL  -- LCD is initialised (configSPI ran in initLCD_hardware)
             *   2. TimeKeeper_isValid() -- SNTP has synced at least once
             *   3. localNow != prevNow  -- the second has actually changed
             * Runs after the poll delay so card detection is never held up. */
            if (spi != NULL && TimeKeeper_isValid())
            {
                prevNow  = localNow;
                localNow = TimeKeeper_getLocalTime();
                if (localNow != prevNow)
                {
                    DateTime_t dt;
                    unixToDateTime(localNow, &dt);
                    uint8_t     hour12 = dt.hour % 12;
                    if (hour12 == 0) hour12 = 12;
                    const char *ampm = (dt.hour < 12) ? "AM" : "PM";
                    char dateBuf[12], timeBuf[12];
                    snprintf(dateBuf, sizeof(dateBuf), "%02u/%02u/%04u",
                             (unsigned)dt.month, (unsigned)dt.day, (unsigned)dt.year);
                    snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u:%02u %s",
                             (unsigned)hour12, (unsigned)dt.minute,
                             (unsigned)dt.second, ampm);
                    drawClockFooter(dateBuf, timeBuf);
                    updateWifiIcon(wifiConnected);
                }
            }
        }
        else
        {
            /* ---- SESSION: yield every 100ms until logout ---- */
            usleep(100000);
        }

        /* ---- SESSION START: spawn bootLCD on 0→1 transition ----
         *
         * bootLCD is launched exactly once per session, here, regardless
         * of which scanning branch (known UID or first-time registration)
         * caused the transition.
         *
         * Stack: 8192 bytes.  openActiveProfile() drives the TI JSON
         * library (~400 bytes internal); drawMenu + fillRect add another
         * 256-byte pixel buffer on the stack.  4096 was too small.       */
        if (sessionActive == 1 && prevActive == 0)
        {
            pthread_t          lcdThread;
            pthread_attr_t     lcdAttrs;
            struct sched_param lcdSched;
            pthread_attr_init(&lcdAttrs);
            lcdSched.sched_priority = 5;
            pthread_attr_setdetachstate(&lcdAttrs, PTHREAD_CREATE_DETACHED);
            pthread_attr_setschedparam(&lcdAttrs, &lcdSched);
            pthread_attr_setstacksize(&lcdAttrs, 8192);
            {
                int lret = pthread_create(&lcdThread, &lcdAttrs, bootLCD, NULL);
                if (lret != 0)
                    Display_printf(display, 0, 0,
                        "bootLCD create FAILED ret=%d\n\r", lret);
                else
                    Display_printf(display, 0, 0,
                        "bootLCD spawned\n\r");
            }
        }
        prevActive = sessionActive;

        /* ---- LOGOUT: checked every iteration, outside both branches ---- */
        if (sessionActive == 0 && activeFile != NULL)
        {
            activeFile = NULL;
            Display_printf(display, 0, 0,
                "Session ended, returning to scan\n\r");

            clearMenuArea();
            drawTextbox(4, MENU_Y + (MENU_H / 2) - FONT_H,
                        LCD_W - 8, FONT_H * 2,
                        "Scan RFID card", COL_FG, COL_BG);
        }
    }
    return NULL;
}

/* ---------------------------------------------------------------
 * ADC
 * --------------------------------------------------------------- */
uint16_t getADC(void)
{
    uint16_t adcValue;
    ADC_Params_init(&adcParams);
    adc = ADC_open(CONFIG_ADC_0, &adcParams);
    if (adc == NULL) { while(1); }
    if (ADC_convert(adc, &adcValue) != ADC_STATUS_SUCCESS)
        Display_printf(display, 0, 0, "ADC error");
    ADC_close(adc);
    return adcValue;
}

/* ---------------------------------------------------------------
 * PWM / Servo
 * --------------------------------------------------------------- */

/* Servo PWM configuration */
#define PWM_FREQ_HZ         50          /* Servo standard: 50 Hz          */
#define PWM_PERIOD_US_VAL   (1000000 / PWM_FREQ_HZ)  /* 20000 uS         */

/* Duty cycle percentages */
#define PWM_DUTY_STOP_PCT       0       /* Motor off                       */
#define PWM_DUTY_FORWARD_PCT    6       /* Forward direction (CW)          */
#define PWM_DUTY_REVERSE_PCT    6       /* Reverse direction (CCW)         */

/* Convert a duty-cycle percentage to microseconds for the current period */
static inline int dutyToUs(int pct)
{
    return (PWM_PERIOD_US_VAL * pct) / 100;
}

void setPWM(int uS)
{
    Display_printf(display, 0, 0, "[ENTER] setPWM\n\r");
    if (pwm != NULL) { PWM_stop(pwm); PWM_close(pwm); pwm = NULL; }
    PWM_Params_init(&params);
    params.idleLevel   = PWM_IDLE_LOW;
    params.periodUnits = PWM_PERIOD_US;
    params.periodValue = PWM_PERIOD_US_VAL;
    params.dutyUnits   = PWM_DUTY_US;
    params.dutyValue   = uS;
    pwm = PWM_open(CONFIG_PWM_0, &params);
    if (pwm == NULL) { while(1); }
    PWM_start(pwm);
}

void* testPWM(void *pvParameters)
{
    const int period = 20000;
    Display_printf(display, 0, 0, "[ENTER] testPWM\n\r");
    int uS = 0;
    double percent = 0.0;
    int adcVal = 0;

    GPIO_write(CONFIG_GPIO_07, 0); GPIO_write(CONFIG_GPIO_10, 0);
    GPIO_write(CONFIG_GPIO_12, 0); GPIO_write(CONFIG_GPIO_06, 0);

    Display_printf(display, 0, 0, "PWM Test Start:\r\n");

    while(1)
    {
        adcVal = getADC();

        if (150 < adcVal && adcVal < 380)
        {
            if (percent > 0.0)
            {
                percent -= 0.02;
                uS = (int)(percent * period);
                Display_printf(display, 0, 0,
                    "PWM time high = %d uS:  %.1f%% Duty Cycle", uS, percent * 100);
                setPWM(uS);
            }
        }
        else if (adcVal > 828)
        {
            if (percent < 1.0)
            {
                percent += 0.02;
                uS = (int)(percent * period);
                Display_printf(display, 0, 0,
                    "PWM time high = %d uS:  %.1f%% Duty Cycle", uS, percent * 100);
                setPWM(uS);
            }
        }
        usleep(100000);
    }
    return NULL;
}

void* testDemux(void* pvParameters)
{
    Display_printf(display, 0, 0, "[ENTER] testDemux\n\r");
    setPWM(10000);
    while(1)
    {
        Display_printf(display, 0, 0, "Servo1, IR1");
        GPIO_write(CONFIG_GPIO_07, 0);
        GPIO_write(CONFIG_GPIO_10, 0);
        GPIO_write(CONFIG_GPIO_12, 0);
        GPIO_write(CONFIG_GPIO_06, 0);
        sleep(10);

        Display_printf(display, 0, 0, "Servo2, IR2");
        GPIO_write(CONFIG_GPIO_07, 1);
        GPIO_write(CONFIG_GPIO_10, 0);
        GPIO_write(CONFIG_GPIO_12, 1);
        GPIO_write(CONFIG_GPIO_06, 0);
        sleep(10);

        Display_printf(display, 0, 0, "Servo3, IR3");
        GPIO_write(CONFIG_GPIO_07, 0);
        GPIO_write(CONFIG_GPIO_10, 1);
        GPIO_write(CONFIG_GPIO_12, 0);
        GPIO_write(CONFIG_GPIO_06, 1);
        sleep(10);

        Display_printf(display, 0, 0, "Servo4, IR4");
        GPIO_write(CONFIG_GPIO_07, 1);
        GPIO_write(CONFIG_GPIO_10, 1);
        GPIO_write(CONFIG_GPIO_12, 1);
        GPIO_write(CONFIG_GPIO_06, 1);
        sleep(10);
    }
    return NULL;
}

/* ---------------------------------------------------------------
 * Dispense
 * --------------------------------------------------------------- */
/* Forward declaration -- defined in json_make.c */
extern int decrementCompartStockByOne(int compartment, const char *pActiveFile);

/* ---------------------------------------------------------------
 * unjamServo()
 *
 * Attempts to clear a pill jam by oscillating the servo slowly
 * in reverse then forward, 5 times, 1 second each direction.
 *
 * PWM values used (PWM_FREQ_HZ, period = PWM_PERIOD_US_VAL uS):
 *   Slow CW  (reverse) : PWM_DUTY_REVERSE_PCT %  -> dutyToUs(PWM_DUTY_REVERSE_PCT)
 *   Slow CCW (forward) : PWM_DUTY_FORWARD_PCT %  -> dutyToUs(PWM_DUTY_FORWARD_PCT)
 *   Stop               : PWM_DUTY_STOP_PCT    %  -> dutyToUs(PWM_DUTY_STOP_PCT)
 *
 * Called immediately after a dispensing timeout in readIR().
 * --------------------------------------------------------------- */
static void unjamServo(void)
{
    int i;
    for (i = 0; i < 5; i++)
    {
        setPWM(dutyToUs(PWM_DUTY_REVERSE_PCT));
        usleep(500000);
        setPWM(dutyToUs(PWM_DUTY_FORWARD_PCT));
        usleep(500000);
    }
    setPWM(dutyToUs(PWM_DUTY_STOP_PCT));
}

int readIR(int container, const char *pActiveFile)
{
    /* Drain any stale posts left from a previous dispense or spurious edge
     * before arming dispenseTracker.  sem_trywait returns -1 when count
     * reaches zero, leaving the semaphore clean for this cycle.         */
    while (sem_trywait(&dispenseSem) == 0) {}

    switch (container)
    {
        case 1: GPIO_write(CONFIG_GPIO_07, 0); GPIO_write(CONFIG_GPIO_10, 0);
                GPIO_write(CONFIG_GPIO_12, 0); GPIO_write(CONFIG_GPIO_06, 0); break;
        case 2: GPIO_write(CONFIG_GPIO_07, 1); GPIO_write(CONFIG_GPIO_10, 0);
                GPIO_write(CONFIG_GPIO_12, 1); GPIO_write(CONFIG_GPIO_06, 0); break;
        case 3: GPIO_write(CONFIG_GPIO_07, 0); GPIO_write(CONFIG_GPIO_10, 1);
                GPIO_write(CONFIG_GPIO_12, 0); GPIO_write(CONFIG_GPIO_06, 1); break;
        case 4: GPIO_write(CONFIG_GPIO_07, 1); GPIO_write(CONFIG_GPIO_10, 1);
                GPIO_write(CONFIG_GPIO_12, 1); GPIO_write(CONFIG_GPIO_06, 1); break;
        default:
            return -1;
    }

    dispenseTracker = container;

    int compartment = container - 1;
    int32_t dose    = getCompartDose(compartment);
    if (dose <= 0) dose = 1;

    int32_t p;
    for (p = 0; p < dose; p++)
    {
        /* Each pill gets 2 dispense attempts (5s each) with one unjam
         * between them.  Pattern: dispense(5s) -> unjam(10s) ->
         * dispense(5s) -> logError -> return -1                  */
        int attempt;
        int pillDetected = 0;

        for (attempt = 0; attempt < 2; attempt++)
        {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 5;

            int ret = sem_timedwait(&dispenseSem, &ts);
            if (ret == 0)
            {
                pillDetected = 1;
                break;   /* pill detected -- exit retry loop */
            }

            if (attempt + 1 < 2)
                unjamServo();
        }

        if (!pillDetected)
        {
            /* Both attempts timed out -- abort and let dispense() log the error */
            dispenseTracker = 0;
            Display_printf(display, 0, 0,
                "readIR: pill %d/%d not detected after 2 attempts -- aborting\n\r",
                (int)(p + 1), (int)dose);
            return -1;
        }

        int decRet = decrementCompartStockByOne(compartment, pActiveFile);
        incrementDispPills(compartment);

        if (decRet < 0 || getCompartStock(compartment) <= 0)
        {
            dispenseTracker = 0;
            return 0;
        }

        usleep(250000);
    }

    dispenseTracker = 0;
    return 0;
}


/* dispense()
 *
 *  1. Check stock for the requested compartment (0-based index = container-1).
 *     If empty, call logError(OutOfPills) via decrementCompartStock and abort.
 *  2. Run the physical dispense (servo + IR confirmation via readIR).
 *  3. On physical failure, call logError(DispensingError) and abort.
 *  4. On success, sync stock with server.
 */
int dispense(int container)
{
    Display_printf(display, 0, 0, "[ENTER] dispense\n\r");
    /* container is 1-based; compartment index is 0-based */
    int compartment = container - 1;

    /* 1. Pre-dispense stock check  */
    int32_t stock = getCompartStock(compartment);
    if (stock <= 0)
    {
        Display_printf(display, 0, 0,
            "dispense: compartment %d empty, aborting\n\r", compartment);
        /* decrementCompartStock handles the logError call */
        decrementCompartStock(compartment, activeFile);
        return -1;
    }

    /* 2. Physical dispense */
    setPWM(dutyToUs(PWM_DUTY_FORWARD_PCT));

    int result = readIR(container, activeFile);
    if (pwm != NULL) { PWM_stop(pwm); PWM_close(pwm); pwm = NULL; }

    if (result != 0)
    {
        logError(DispensingError, activeFile, compartment);
        return -1;
    }

    /* Servo is now idle.  POST compStock and refresh from the server's
     * response — picks up any pills added via the web app since last sync. */
    syncStockWithServer();

    return 0;
}
