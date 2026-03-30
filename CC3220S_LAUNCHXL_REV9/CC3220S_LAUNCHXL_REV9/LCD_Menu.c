#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <ti/drivers/GPIO.h>
#include <ti/drivers/SPI.h>
#include <ti/display/Display.h>
#include <ti/drivers/PWM.h>
#include "ti_drivers_config.h"
#include <semaphore.h>
#include <LCD_Resources.h>
#include "LCD_Menu.h"
#include "drive_gpio.h"

extern Display_Handle display;
extern sem_t          sem;
extern PWM_Handle     pwm;
extern PWM_Params     params;
extern SPI_Handle     spi;

/* Forward declarations for dispense — defined in drive_gpio.c */
int dispense(int container);

/* ---------------------------------------------------------------
 * Shared navigation state
 * --------------------------------------------------------------- */
MENU_State currentMenu   = MENU_MAIN;
int        ADCTracker[2] = {0, 0};

/* ---------------------------------------------------------------
 * Menu data
 * --------------------------------------------------------------- */
static const char *subMenuDispense[] = {
    "P1:dose:pills",
    "P2:dose:pills",
    "P3:dose:pills",
    "P4:dose:pills",
    "Exit",
};

static const char *subMenuAccount[] = {
    "Profile",
    "Settings",
    "Exit",
};

static const char *subMenuNot[] = {
    "Device1",
    "Device2",
    "Device3",
    "Device4",
    "Device5",
    "Exit",
};

static const char *menuItems[2][2] = {
    { "Dispense",  "My Account" },
    { "Alerts",    "Exit"       },
};

#define ARRAY_LEN(a)  (sizeof(a) / sizeof((a)[0]))

typedef struct
{
    uint8_t rows;
    uint8_t cols;
} MENU_Dim;

static const MENU_Dim menuDims[] = {
    [MENU_MAIN]          = { .rows = 2,                          .cols = 2 },
    [MENU_DISPENSE]      = { .rows = ARRAY_LEN(subMenuDispense), .cols = 1 },
    [MENU_ACCOUNT]       = { .rows = ARRAY_LEN(subMenuAccount),  .cols = 1 },
    [MENU_NOTIFICATIONS] = { .rows = ARRAY_LEN(subMenuNot),      .cols = 1 },
};

/* ---------------------------------------------------------------
 * LCD primitives
 * --------------------------------------------------------------- */
void writeCmd(uint8_t cmd)
{
    GPIO_write(CONFIG_GPIO_00, 0);
    GPIO_write(CONFIG_GPIO_09, 0);
    SPI_Transaction t = {.count=1, .txBuf=&cmd, .rxBuf=NULL};
    SPI_transfer(spi, &t);
    GPIO_write(CONFIG_GPIO_09, 1);
}

void writeData(uint8_t data)
{
    GPIO_write(CONFIG_GPIO_00, 1);
    GPIO_write(CONFIG_GPIO_09, 0);
    SPI_Transaction t = {.count=1, .txBuf=&data, .rxBuf=NULL};
    SPI_transfer(spi, &t);
    GPIO_write(CONFIG_GPIO_09, 1);
}

void writeDataBulk(uint8_t *buf, size_t len)
{
    GPIO_write(CONFIG_GPIO_00, 1);
    GPIO_write(CONFIG_GPIO_09, 0);
    SPI_Transaction t = {.count=len, .txBuf=buf, .rxBuf=NULL};
    SPI_transfer(spi, &t);
    GPIO_write(CONFIG_GPIO_09, 1);
}

void initLCD(void)
{
    GPIO_write(CONFIG_GPIO_30, 1); usleep(5000);
    GPIO_write(CONFIG_GPIO_30, 0); usleep(20000);
    GPIO_write(CONFIG_GPIO_30, 1); usleep(150000);
    writeCmd(0x01); usleep(150000);
    writeCmd(0x11); usleep(120000);
    writeCmd(0x3A); writeData(0x55);
    writeCmd(0x36); writeData(0xE8);
    writeCmd(0xC0); writeData(0x23);
    writeCmd(0xC1); writeData(0x10);
    writeCmd(0xC5); writeData(0x3E); writeData(0x28);
    writeCmd(0xC7); writeData(0x86);
    writeCmd(0x29); usleep(100000);
}

void setAddrWindow(int x1, int y1, int x2, int y2)
{
    writeCmd(0x2A);
    writeData(x1>>8); writeData(x1&0xFF);
    writeData(x2>>8); writeData(x2&0xFF);
    writeCmd(0x2B);
    writeData(y1>>8); writeData(y1&0xFF);
    writeData(y2>>8); writeData(y2&0xFF);
    writeCmd(0x2C);
}

void sendByte(uint8_t byte)
{
    SPI_Transaction t = {.count=1, .txBuf=&byte, .rxBuf=NULL};
    SPI_transfer(spi, &t);
}

void fillRect(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) return;
    setAddrWindow(x, y, x+w-1, y+h-1);
    #define FILL_BUF_PIXELS 128
    uint8_t buf[FILL_BUF_PIXELS * 2];
    uint8_t hi = color >> 8, lo = color & 0xFF;
    for (int i = 0; i < FILL_BUF_PIXELS*2; i+=2) { buf[i]=hi; buf[i+1]=lo; }
    uint32_t total = (uint32_t)w * h;
    uint32_t full  = total / FILL_BUF_PIXELS;
    uint32_t rem   = total % FILL_BUF_PIXELS;
    GPIO_write(CONFIG_GPIO_00, 1);
    GPIO_write(CONFIG_GPIO_09, 0);
    SPI_Transaction t;
    for (uint32_t i = 0; i < full; i++) {
        t.count=FILL_BUF_PIXELS*2; t.txBuf=buf; t.rxBuf=NULL;
        SPI_transfer(spi, &t);
    }
    if (rem) { t.count=rem*2; t.txBuf=buf; t.rxBuf=NULL; SPI_transfer(spi,&t); }
    GPIO_write(CONFIG_GPIO_09, 1);
}

void redTest(void)
{
    writeCmd(0x2A); writeData(0);writeData(0);writeData(1);writeData(63);
    writeCmd(0x2B); writeData(0);writeData(0);writeData(0);writeData(239);
    writeCmd(0x2C);
    uint32_t n = 320*240;
    while(n--) { writeData(0xF8); writeData(0x00); }
}

void drawChar(int x, int y, char c, uint16_t fg, uint16_t bg)
{
    if (x < 0 || y < 0 || x + CHAR_W > LCD_W || y + FONT_H > LCD_H) return;
    uint8_t buf[CHAR_W * FONT_H * 2];
    int idx = 0;
    for (int row = 0; row < FONT_H; row++) {
        for (int col = 0; col < FONT_W; col++) {
            uint8_t colbyte = font[(uint8_t)c * FONT_W + col];
            uint16_t color = (colbyte & (1 << row)) ? fg : bg;
            buf[idx++] = color >> 8;
            buf[idx++] = color & 0xFF;
        }
        buf[idx++] = bg >> 8;
        buf[idx++] = bg & 0xFF;
    }
    setAddrWindow(x, y, x+CHAR_W-1, y+FONT_H-1);
    writeDataBulk(buf, sizeof(buf));
}

int drawTextbox(int x, int y, int maxW, int maxH,
                const char *str, uint16_t fg, uint16_t bg)
{
    int cols = maxW / CHAR_W;
    int rows = maxH / FONT_H;
    int cx = 0, cy = 0;
    for (int i = 0; str[i] != '\0'; i++) {
        char c = str[i];
        if (c == '\n') {
            while (cx < cols) { drawChar(x+cx*CHAR_W, y+cy*FONT_H, ' ', fg, bg); cx++; }
            cx = 0; cy++;
            if (cy >= rows) break;
            continue;
        }
        if (cx >= cols) { cx = 0; cy++; if (cy >= rows) break; }
        drawChar(x+cx*CHAR_W, y+cy*FONT_H, c, fg, bg);
        cx++;
    }
    if (cy < rows) {
        while (cx < cols) { drawChar(x+cx*CHAR_W, y+cy*FONT_H, ' ', fg, bg); cx++; }
    }
    return y + (cy+1)*FONT_H;
}

/* ---------------------------------------------------------------
 * Menu helpers
 * --------------------------------------------------------------- */
static const char* getLabel(MENU_State menu, int row, int col)
{
    switch (menu)
    {
        case MENU_MAIN:          return menuItems[row][col];
        case MENU_DISPENSE:      return subMenuDispense[row];
        case MENU_ACCOUNT:       return subMenuAccount[row];
        case MENU_NOTIFICATIONS: return subMenuNot[row];
        default:                 return "";
    }
}

static void drawCell(int row, int col, MENU_State menu, bool selected)
{
    int cols  = menuDims[menu].cols;
    int rows  = menuDims[menu].rows;
    int cellW = LCD_W  / cols;
    int cellH = MENU_H / rows;

    int x = col * cellW;
    int y = MENU_Y + row * cellH;

    const char *label = getLabel(menu, row, col);
    int         textW = strlen(label) * CHAR_W;

    int hlX = x + 4 - HIGHLIGHT_PAD;
    int hlY = y + 2 - HIGHLIGHT_PAD;
    int hlW = textW + HIGHLIGHT_PAD * 2;
    int hlH = FONT_H + HIGHLIGHT_PAD * 2;

    if (hlX < x)               hlX = x;
    if (hlY < y)               hlY = y;
    if (hlX + hlW > x + cellW) hlW = x + cellW - hlX;
    if (hlY + hlH > y + cellH) hlH = y + cellH - hlY;

    fillRect(x, y, cellW, cellH, COL_BG);

    if (selected)
        fillRect(hlX, hlY, hlW, hlH, COL_SEL_BG);

    uint16_t textBg = selected ? COL_SEL_BG : COL_BG;
    drawTextbox(x + 4, y + 2, textW, FONT_H, label, COL_FG, textBg);
}

void drawMenu(int selRow, int selCol, MENU_State menu)
{
    int rows = menuDims[menu].rows;
    int cols = menuDims[menu].cols;
    for (int row = 0; row < rows; row++)
        for (int col = 0; col < cols; col++)
            drawCell(row, col, menu, (row == selRow && col == selCol));
}

void cursorSelect(MENU_State menu, int row, int col)
{
    switch (menu)
    {
        case MENU_MAIN:
            switch (row)
            {
                case 0:
                    switch (col)
                    {
                        case 0:
                            fillRect(0, MENU_Y, LCD_W, MENU_H, COL_BG);
                            currentMenu   = MENU_DISPENSE;
                            ADCTracker[0] = 0;
                            ADCTracker[1] = 0;
                            drawMenu(0, 0, MENU_DISPENSE);
                            break;
                        case 1:
                            fillRect(0, MENU_Y, LCD_W, MENU_H, COL_BG);
                            currentMenu   = MENU_ACCOUNT;
                            ADCTracker[0] = 0;
                            ADCTracker[1] = 0;
                            drawMenu(0, 0, MENU_ACCOUNT);
                            break;
                        default: break;
                    }
                    break;
                case 1:
                    switch (col)
                    {
                        case 0:
                            fillRect(0, MENU_Y, LCD_W, MENU_H, COL_BG);
                            currentMenu   = MENU_NOTIFICATIONS;
                            ADCTracker[0] = 0;
                            ADCTracker[1] = 0;
                            drawMenu(0, 0, MENU_NOTIFICATIONS);
                            break;
                        case 1: /* Exit — TODO */ break;
                        default: break;
                    }
                    break;
                default: break;
            }
            break;

        case MENU_DISPENSE:
            switch (row)
            {
                case 0: dispense(1); break;
                case 1: dispense(2); break;
                case 2: dispense(3); break;
                case 3: dispense(4); break;
                case 4:
                    fillRect(0, MENU_Y, LCD_W, MENU_H, COL_BG);
                    currentMenu   = MENU_MAIN;
                    ADCTracker[0] = 0;
                    ADCTracker[1] = 0;
                    drawMenu(0, 0, MENU_MAIN);
                    break;
                default: break;
            }
            break;

        case MENU_ACCOUNT:
            switch (row)
            {
                case 0: /* TODO: profile   */ break;
                case 1: /* TODO: settings  */ break;
                case 2:
                    fillRect(0, MENU_Y, LCD_W, MENU_H, COL_BG);
                    currentMenu   = MENU_MAIN;
                    ADCTracker[0] = 0;
                    ADCTracker[1] = 0;
                    drawMenu(0, 0, MENU_MAIN);
                    break;
                default: break;
            }
            break;

        case MENU_NOTIFICATIONS:
            switch (row)
            {
                case 0: /* TODO: Device1 */ break;
                case 1: /* TODO: Device2 */ break;
                case 2: /* TODO: Device3 */ break;
                case 3: /* TODO: Device4 */ break;
                case 4: /* TODO: Device5 */ break;
                case 5:
                    fillRect(0, MENU_Y, LCD_W, MENU_H, COL_BG);
                    currentMenu   = MENU_MAIN;
                    ADCTracker[0] = 0;
                    ADCTracker[1] = 0;
                    drawMenu(0, 0, MENU_MAIN);
                    break;
                default: break;
            }
            break;

        default: break;
    }
}

/* ---------------------------------------------------------------
 * ADC tracking thread
 * --------------------------------------------------------------- */
void* trackADC(void *pvParameters)
{
    uint16_t adcValue;
    int rows = menuDims[currentMenu].rows;
    int cols = menuDims[currentMenu].cols;

    int prevRow = ADCTracker[0];
    int prevCol = ADCTracker[1];

    char idxBuf[24];
    snprintf(idxBuf, sizeof(idxBuf), "[%d,%d]", ADCTracker[0], ADCTracker[1]);
    fillRect(0, LCD_H - FOOTER_H, LCD_W, FOOTER_H, 0x4208);
    drawTextbox(4, LCD_H - FOOTER_H + 2, LCD_W - 8, FOOTER_H - 4, idxBuf, 0xFFFF, 0x4208);

    while(1)
    {
        sem_wait(&sem);

        /* getADC is defined in drive_gpio.c */
        extern uint16_t getADC(void);
        adcValue = getADC();

        if (adcValue < 50)
        {
            sem_post(&sem);
            usleep(10000);
            continue;
        }
        else if (adcValue < 130)  { if (ADCTracker[0] > 0)        ADCTracker[0]--; }
        else if (adcValue < 250)  { if (ADCTracker[0] < rows - 1) ADCTracker[0]++; }
        else if (adcValue < 360)  { if (ADCTracker[1] > 0)        ADCTracker[1]--; }
        else if (adcValue < 560)  { if (ADCTracker[1] < cols - 1) ADCTracker[1]++; }
        else if (adcValue < 720)
        {
            cursorSelect(currentMenu, ADCTracker[0], ADCTracker[1]);
            rows    = menuDims[currentMenu].rows;
            cols    = menuDims[currentMenu].cols;
            prevRow = ADCTracker[0];
            prevCol = ADCTracker[1];
        }

        if (ADCTracker[0] != prevRow || ADCTracker[1] != prevCol)
        {
            drawCell(prevRow, prevCol, currentMenu, false);
            drawCell(ADCTracker[0], ADCTracker[1], currentMenu, true);

            snprintf(idxBuf, sizeof(idxBuf), "[%d,%d]", ADCTracker[0], ADCTracker[1]);
            fillRect(0, LCD_H - FOOTER_H, LCD_W, FOOTER_H, 0x4208);
            drawTextbox(4, LCD_H - FOOTER_H + 2, LCD_W - 8, FOOTER_H - 4, idxBuf, 0xFFFF, 0x4208);

            prevRow = ADCTracker[0];
            prevCol = ADCTracker[1];
        }

        sem_post(&sem);
        usleep(50000);
    }
    return NULL;
}

/* ---------------------------------------------------------------
 * Boot thread
 * --------------------------------------------------------------- */
void* bootLCD(void* pvParameters)
{
    sem_wait(&sem);

    extern void configSPI(void);
    extern void configInterrupt(void);
    configSPI();
    configInterrupt();
    initLCD();
    redTest();
    usleep(2000000);

    fillRect(0, 0, LCD_W, LCD_H, COL_BG);

    fillRect(0, 0, LCD_W, HEADER_H, 0x001F);
    drawTextbox(4, 4, LCD_W-8, HEADER_H-4, "EasyMedRx", 0xFFFF, 0x001F);

    fillRect(0, LCD_H-FOOTER_H, LCD_W, FOOTER_H, 0x4208);
    drawTextbox(4, LCD_H-FOOTER_H+2, LCD_W-8, FOOTER_H-4, "Status: Ready", 0xFFFF, 0x4208);

    drawMenu(0, 0, MENU_MAIN);

    pthread_t trackThread;
    pthread_attr_t trackAttrs;
    struct sched_param trackSched;
    pthread_attr_init(&trackAttrs);
    trackSched.sched_priority = 2;
    pthread_attr_setdetachstate(&trackAttrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&trackAttrs, &trackSched);
    pthread_attr_setstacksize(&trackAttrs, 4096);
    pthread_create(&trackThread, &trackAttrs, trackADC, NULL);
    Display_printf(display, 0, 0, "\rtrackADC() scheduled: prio 2\n\r");

    sem_post(&sem);
    return NULL;
}
