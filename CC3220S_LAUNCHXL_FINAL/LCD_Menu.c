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
#include "json_make.h"      /* getCompartStock(), getCompartDose() */
#include "wifi_Icons.h"     /* noWifiIcon 32x32 RGB565 bitmap */
#include "timeKeep.h"       /* TimeKeeper_getLocalTime(), unixToDateTime() */
extern volatile uint8_t sessionActive;  /* defined in json_make.c */
extern void *RC522_senseRFID(void *pvParameters); /* drive_gpio.c */
extern void  setSPIRate(uint32_t hz);                  /* drive_gpio.c */

extern Display_Handle display;
extern sem_t          sem;

/* Shared clock buffer written by TimeKeeper_thread, drawn by trackADC.
 * volatile uint8_t flag is atomic on Cortex-M4 -- no mutex needed.   */
volatile uint8_t g_clockDirty = 0;
char             g_clockDate[12] = {0};
char             g_clockTime[12] = {0};

extern PWM_Handle     pwm;
extern PWM_Params     params;
extern SPI_Handle     spi;

/* Forward declarations for dispense defined in drive_gpio.c */
int dispense(int container);

/* ---------------------------------------------------------------
 * Wi-Fi connection state
 * Set to 0 (no wifi) or 1 (connected) by http_comm.c.
 * Read by drawNoWifiIcon() and updateWifiIcon().
 * --------------------------------------------------------------- */
volatile uint8_t wifiConnected = 0;

/* Forward declaration — defined after the public write primitives */
static void _setAddrWindow(int x1, int y1, int x2, int y2);

/* The wifi icon is 32x32 and starts at y=0 (clamped from (HEADER_H-32)/2=-6).
 * Because the icon is taller than HEADER_H (20 px), the bottom
 *   WIFI_ICON_BLEED = 32 - HEADER_H = 12 rows
 * overlap the top of the menu area (y=20..31, x=288..319).
 *
 * clearMenuArea() preserves that overlap so button-press redraws never
 * erase the icon bottom, eliminating the flicker between the fillRect
 * and the subsequent updateWifiIcon call.                               */
#define WIFI_ICON_W     32
#define WIFI_ICON_X     (LCD_W - WIFI_ICON_W)           /* 288 */
#define WIFI_ICON_BLEED (WIFI_ICON_W - HEADER_H)        /* 12  */

/* Fill a rectangle while preserving the wifi icon bleed region:
 *   x = [WIFI_ICON_X .. LCD_W-1]  (288..319)
 *   y = [MENU_Y     .. MENU_Y+WIFI_ICON_BLEED-1]  (20..31)
 *
 * The rect is split into up to three sub-rects that avoid the bleed zone.
 * Falls through to a plain fillRect when there is no overlap.
 * Used by drawCell() so that individual cell clears never erase the
 * bottom rows of the wifi icon bitmap.                                  */
static void fillRectSafe(int x, int y, int w, int h, uint16_t color)
{
    const int bx  = WIFI_ICON_X;                   /* 288 — icon left edge  */
    const int by  = MENU_Y;                         /* 20  — bleed top       */
    const int bry = MENU_Y + WIFI_ICON_BLEED;       /* 32  — bleed bottom    */
    int rx = x + w;                                 /* rect right  (exclusive) */
    int ry = y + h;                                 /* rect bottom (exclusive) */

    /* No overlap with the bleed zone → plain fill */
    if (rx <= bx || ry <= by || y >= bry)
    {
        fillRect(x, y, w, h, color);
        return;
    }

    /* Left strip: x < bx — safe for the full cell height */
    if (x < bx)
        fillRect(x, y, bx - x, h, color);

    /* Right strip (overlaps icon column): skip the bleed rows */
    {
        int sx = (x >= bx) ? x : bx;   /* start x of right strip */
        int sw = rx - sx;               /* width                  */
        if (sw > 0)
        {
            if (y < by)                 /* portion above bleed    */
                fillRect(sx, y, sw, by - y, color);
            if (ry > bry)              /* portion below bleed    */
                fillRect(sx, bry, sw, ry - bry, color);
            /* rows inside [by, bry) are the icon — leave them alone */
        }
    }
}

void clearMenuArea(void)
{
    /* Left portion: full height, stops before the icon column */
    fillRect(0, MENU_Y, WIFI_ICON_X, MENU_H, COL_BG);
    /* Right column: starts below the icon bleed region */
    fillRect(WIFI_ICON_X, MENU_Y + WIFI_ICON_BLEED,
             WIFI_ICON_W, MENU_H - WIFI_ICON_BLEED, COL_BG);
}

/* Draw the no-wifi icon flush to the top-right of the header.
 * The bitmap is 32x32; the bottom 12 rows bleed into the menu area. */
static void drawNoWifiIcon(void)
{
    int ix = LCD_W - 32;
    int iy = (HEADER_H - 32) / 2;
    if (iy < 0) iy = 0;
    uint8_t buf[32 * 2];
    int row, col;
    for (row = 0; row < 32; row++)
    {
        for (col = 0; col < 32; col++)
        {
            uint16_t px = noWifiIcon[row][col];
            buf[col * 2]     = px >> 8;
            buf[col * 2 + 1] = px & 0xFF;
        }
        pthread_mutex_lock(&g_spiMutex);
        _setAddrWindow(ix, iy + row, ix + 31, iy + row);
        GPIO_write(CONFIG_GPIO_30, 1);   /* DC high = data   */
        GPIO_write(CONFIG_GPIO_00, 0);   /* CS low  = select */
        SPI_Transaction t = {.count=sizeof(buf), .txBuf=buf, .rxBuf=NULL};
        SPI_transfer(spi, &t);
        GPIO_write(CONFIG_GPIO_00, 1);   /* CS high = deselect */
        pthread_mutex_unlock(&g_spiMutex);
    }
}

/* Erase the icon area by filling with header colour.
 * Clipped to HEADER_H rows so the bleed region in the menu area
 * is not painted over — clearMenuArea() handles those rows separately. */
static void clearNoWifiIcon(void)
{
    int ix = LCD_W - 32;
    int iy = (HEADER_H - 32) / 2;
    if (iy < 0) iy = 0;
    int clearH = HEADER_H - iy;        /* rows within the header only */
    fillRect(ix, iy, 32, clearH, 0x001F);

    /* Restore the bleed rows in the menu area to the menu background */
    fillRect(ix, MENU_Y, 32, WIFI_ICON_BLEED, COL_BG);
}

/* ---------------------------------------------------------------
 * drawClockFooter()
 *
 * Renders the current local time in the bottom-right corner of the
 * footer bar:
 *   Line 1: MM/DD/YYYY   (x=252, y=222)
 *   Line 2: HH:MM:SS AM  (x=252, y=230)
 *
 * Clears only the clock region (x=250..319) before redrawing so the
 * ADC index on the left side of the footer is not disturbed.
 * Called by TimeKeeper_thread every second after first SNTP sync.
 * --------------------------------------------------------------- */
void drawClockFooter(const char *dateLine, const char *timeLine)
{
    /* Clear the right portion of the footer */
    fillRect(250, LCD_H - FOOTER_H, LCD_W - 250, FOOTER_H, 0x4208);

    /* Date line */
    drawTextbox(252, LCD_H - FOOTER_H + 2,
                (LCD_W - 254) * 1, FONT_H,
                dateLine, 0xFFFF, 0x4208);

    /* Time line */
    drawTextbox(252, LCD_H - FOOTER_H + 2 + FONT_H + 1,
                (LCD_W - 254) * 1, FONT_H,
                timeLine, 0xFFFF, 0x4208);
}

/* Called by http_comm.c after each POST attempt.
 * connected: 1 = POST succeeded, clear the no-wifi icon.
 *            0 = server unreachable, show the no-wifi icon. */
void updateWifiIcon(uint8_t connected)
{
    wifiConnected = connected;
    if (connected)
        clearNoWifiIcon();
    else
        drawNoWifiIcon();

    /* Redraw clock footer -- wifi icon draw can overwrite the header region
     * and menu transitions wipe the full screen, so refresh the clock here
     * after every icon update so it always appears in the bottom-right. */
    if (TimeKeeper_isValid())
    {
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
        drawClockFooter(dateBuf, timeBuf);
    }
}

/* ---------------------------------------------------------------
 * Shared navigation state
 * --------------------------------------------------------------- */
MENU_State currentMenu   = MENU_MAIN;
int        ADCTracker[2] = {0, 0};

/* ---------------------------------------------------------------
 * Menu data
 * --------------------------------------------------------------- */

/* Dispense labels are built at runtime to show live stock counts.
 * Format: "P#: NNNN pills" 20 chars is enough for any int32.   */
#define DISPENSE_LABEL_LEN  40
static char dispenseLabel0[DISPENSE_LABEL_LEN];
static char dispenseLabel1[DISPENSE_LABEL_LEN];
static char dispenseLabel2[DISPENSE_LABEL_LEN];
static char dispenseLabel3[DISPENSE_LABEL_LEN];

static const char *subMenuDispense[] = {
    dispenseLabel0,
    dispenseLabel1,
    dispenseLabel2,
    dispenseLabel3,
    "Exit",
};

static const char *subMenuAccount[] = {
    "Profile",
    "Settings",
    "Exit",
};

/* Device labels are populated at runtime from getDeviceName().
 * If no name is stored the slot shows "unassigned".              */
#define DEVICE_LABEL_LEN  40
static char deviceLabel0[DEVICE_LABEL_LEN];
static char deviceLabel1[DEVICE_LABEL_LEN];
static char deviceLabel2[DEVICE_LABEL_LEN];
static char deviceLabel3[DEVICE_LABEL_LEN];
static char deviceLabel4[DEVICE_LABEL_LEN];

static const char *subMenuNot[] = {
    deviceLabel0,
    deviceLabel1,
    deviceLabel2,
    deviceLabel3,
    deviceLabel4,
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
 * Dispense authorization mask
 * Computed once by matchCompartments() when the dispense menu is
 * entered. Bit N set = compartment N is prescribed for this user.
 * --------------------------------------------------------------- */
static uint8_t dispenseMask = 0;

/* ---------------------------------------------------------------
 * Dispense label refresh
 * Pulls script name and dose from the active JSON profile (jsonObjHandle)
 * via getScriptName()/getProfileDose(), and stock from the compartment
 * stock object (csObjHandle) via getCompartStock().
 * Unauthorized compartments (not in dispenseMask) show "Unauthorized" --
 * isRowSelectable() detects this string and blocks navigation and selection.
 * Call this whenever the dispense menu is about to be drawn.
 * --------------------------------------------------------------- */
void refreshDispenseLabels(void)
{
    char *labels[4] = { dispenseLabel0, dispenseLabel1,
                        dispenseLabel2, dispenseLabel3 };
    int i;
    for (i = 0; i < 4; i++)
    {
        if (!(dispenseMask & (1u << i)))
        {
            snprintf(labels[i], DISPENSE_LABEL_LEN, "Unauthorized");
            continue;
        }

        int32_t stock = getCompartStock(i);
        int32_t dose  = getProfileDose(i);
        char    scriptName[24] = {0};
        getScriptName(i, scriptName, sizeof(scriptName));

        /* Label: "<script> - <dose> pills - <stock> left" */
        if (stock < 0)
            snprintf(labels[i], DISPENSE_LABEL_LEN, "ERROR");
        else if (stock == 0)
            snprintf(labels[i], DISPENSE_LABEL_LEN, "%.12s - %d pills - EMPTY",
                     scriptName, (int)dose);
        else
            snprintf(labels[i], DISPENSE_LABEL_LEN, "%.12s - %d pills - %d left",
                     scriptName, (int)dose, (int)stock);
    }
}

/* ---------------------------------------------------------------
 * Device label refresh
 * Reads device0-4 from the active JSON profile via getDeviceName().
 * Rows show "Device N: <name>" or "Device N: unassigned" when empty.
 * These rows are display-only — isRowSelectable() returns false for them.
 * Call this whenever the notifications menu is about to be drawn.
 * --------------------------------------------------------------- */
static void refreshDeviceLabels(void)
{
    char *labels[5] = { deviceLabel0, deviceLabel1, deviceLabel2,
                        deviceLabel3, deviceLabel4 };
    int i;
    for (i = 0; i < 5; i++)
    {
        char name[24] = {0};
        int rc = getDeviceName(i, name, sizeof(name));
        if (rc < 0 || name[0] == '\0')
            snprintf(labels[i], DEVICE_LABEL_LEN, "Device %d: unassigned", i + 1);
        else
            snprintf(labels[i], DEVICE_LABEL_LEN, "Device %d: %.20s", i + 1, name);
    }
}

/* ---------------------------------------------------------------
 * LCD primitives Mutexes implemented to avoid data corruption
 * --------------------------------------------------------------- */
/* ---- unlocked primitives (caller must hold g_spiMutex) ----------- */
/*
 * Pin assignments (corrected):
 *   CONFIG_GPIO_30 = DC  (Data/Command)  -- low=command, high=data
 *   CONFIG_GPIO_00 = CS  (Chip Select)   -- active low
 *   CONFIG_GPIO_09 = RST (Reset)         -- active low
 */
static void _writeCmdDirect(uint8_t cmd)
{
    GPIO_write(CONFIG_GPIO_30, 0);   /* DC low  = command */
    GPIO_write(CONFIG_GPIO_00, 0);   /* CS low  = select  */
    SPI_Transaction t = {.count=1, .txBuf=&cmd, .rxBuf=NULL};
    SPI_transfer(spi, &t);
    GPIO_write(CONFIG_GPIO_00, 1);   /* CS high = deselect */
}

static void _writeDataDirect(uint8_t data)
{
    GPIO_write(CONFIG_GPIO_30, 1);   /* DC high = data    */
    GPIO_write(CONFIG_GPIO_00, 0);   /* CS low  = select  */
    SPI_Transaction t = {.count=1, .txBuf=&data, .rxBuf=NULL};
    SPI_transfer(spi, &t);
    GPIO_write(CONFIG_GPIO_00, 1);   /* CS high = deselect */
}

static void _setAddrWindow(int x1, int y1, int x2, int y2)
{
    _writeCmdDirect(0x2A);
    _writeDataDirect(x1>>8); _writeDataDirect(x1&0xFF);
    _writeDataDirect(x2>>8); _writeDataDirect(x2&0xFF);
    _writeCmdDirect(0x2B);
    _writeDataDirect(y1>>8); _writeDataDirect(y1&0xFF);
    _writeDataDirect(y2>>8); _writeDataDirect(y2&0xFF);
    _writeCmdDirect(0x2C);
}
/* ------------------------------------------------------------------ */

void writeCmd(uint8_t cmd)
{
    pthread_mutex_lock(&g_spiMutex);
    _writeCmdDirect(cmd);
    pthread_mutex_unlock(&g_spiMutex);
}

void writeData(uint8_t data)
{
    pthread_mutex_lock(&g_spiMutex);
    _writeDataDirect(data);
    pthread_mutex_unlock(&g_spiMutex);
}

void writeDataBulk(uint8_t *buf, size_t len)
{
    pthread_mutex_lock(&g_spiMutex);
    GPIO_write(CONFIG_GPIO_30, 1);   /* DC high = data   */
    GPIO_write(CONFIG_GPIO_00, 0);   /* CS low  = select */
    /*usleep(5);*/
    SPI_Transaction t = {.count=len, .txBuf=buf, .rxBuf=NULL};
    SPI_transfer(spi, &t);
    /*usleep(5);*/
    GPIO_write(CONFIG_GPIO_00, 1);   /* CS high = deselect */
    pthread_mutex_unlock(&g_spiMutex);
}

void initLCD(void)
{
    Display_printf(display, 0, 0, "[ENTER] initLCD\n\r");
    GPIO_write(CONFIG_GPIO_09, 1); usleep(5000);    /* RST high */
    GPIO_write(CONFIG_GPIO_09, 0); usleep(20000);   /* RST low  */
    GPIO_write(CONFIG_GPIO_09, 1); usleep(150000);  /* RST high -- hardware reset done */
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
    #define FILL_BUF_PIXELS 128
    uint8_t buf[FILL_BUF_PIXELS * 2];
    uint8_t hi = color >> 8, lo = color & 0xFF;
    for (int i = 0; i < FILL_BUF_PIXELS*2; i+=2) { buf[i]=hi; buf[i+1]=lo; }
    uint32_t total = (uint32_t)w * h;
    uint32_t full  = total / FILL_BUF_PIXELS;
    uint32_t rem   = total % FILL_BUF_PIXELS;
    pthread_mutex_lock(&g_spiMutex);
    _setAddrWindow(x, y, x+w-1, y+h-1);
    GPIO_write(CONFIG_GPIO_30, 1);   /* DC high = data   */
    GPIO_write(CONFIG_GPIO_00, 0);   /* CS low  = select */
    SPI_Transaction t;
    for (uint32_t i = 0; i < full; i++) {
        t.count=FILL_BUF_PIXELS*2; t.txBuf=buf; t.rxBuf=NULL;
        SPI_transfer(spi, &t);
    }
    if (rem) { t.count=rem*2; t.txBuf=buf; t.rxBuf=NULL; SPI_transfer(spi,&t); }
    GPIO_write(CONFIG_GPIO_00, 1);   /* CS high = deselect */
    pthread_mutex_unlock(&g_spiMutex);
}

void redTest(void)
{
    Display_printf(display, 0, 0, "[ENTER] redTest\n\r");
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
    pthread_mutex_lock(&g_spiMutex);
    _setAddrWindow(x, y, x+CHAR_W-1, y+FONT_H-1);
    GPIO_write(CONFIG_GPIO_30, 1);   /* DC high = data   */
    GPIO_write(CONFIG_GPIO_00, 0);   /* CS low  = select */
    SPI_Transaction t = {.count=sizeof(buf), .txBuf=buf, .rxBuf=NULL};
    SPI_transfer(spi, &t);
    GPIO_write(CONFIG_GPIO_00, 1);   /* CS high = deselect */
    pthread_mutex_unlock(&g_spiMutex);
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
    Display_printf(display, 0, 0, "[ENTER] getLabel\n\r");
    switch (menu)
    {
        case MENU_MAIN:          return menuItems[row][col];
        case MENU_DISPENSE:      return subMenuDispense[row];
        case MENU_ACCOUNT:       return subMenuAccount[row];
        case MENU_NOTIFICATIONS: return subMenuNot[row];
        default:                 return "";
    }
}

/* Returns false for rows that are display-only and must never be
 * navigated to or selected:
 *   - Any row whose label contains "Unauthorized" (dispense mask)
 *   - MENU_NOTIFICATIONS rows 0-4 (device info labels, read-only) */
static bool isRowSelectable(MENU_State menu, int row)
{
    Display_printf(display, 0, 0, "[ENTER] isRowSelectable\n\r");
    if (menu == MENU_NOTIFICATIONS && row < 5)
        return false;
    const char *label = getLabel(menu, row, 0);
    return (strstr(label, "Unauthorized") == NULL);
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

    fillRectSafe(x, y, cellW, cellH, COL_BG);

    if (selected)
        fillRectSafe(hlX, hlY, hlW, hlH, COL_SEL_BG);

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
    updateWifiIcon(wifiConnected);
}

void cursorSelect(MENU_State menu, int row, int col)
{
    Display_printf(display, 0, 0, "[ENTER] cursorSelect\n\r");
    switch (menu)
    {
        case MENU_MAIN:
            switch (row)
            {
                case 0:
                    switch (col)
                    {
                        case 0:
                            clearMenuArea();
                            currentMenu   = MENU_DISPENSE;
                            ADCTracker[1] = 0;
                            dispenseMask  = matchCompartments(); /* cross-check prescriptions */
                            refreshDispenseLabels();
                            /* Start cursor on first selectable row, not necessarily row 0 */
                            {
                                int startRow = 0;
                                int nRows = (int)ARRAY_LEN(subMenuDispense);
                                while (startRow < nRows && !isRowSelectable(MENU_DISPENSE, startRow))
                                    startRow++;
                                ADCTracker[0] = (startRow < nRows) ? startRow : 0;
                            }
                            drawMenu(ADCTracker[0], 0, MENU_DISPENSE);
                            break;
                        case 1:
                            clearMenuArea();
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
                            clearMenuArea();
                            currentMenu   = MENU_NOTIFICATIONS;
                            ADCTracker[1] = 0;
                            refreshDeviceLabels();
                            /* Rows 0-4 are non-selectable device labels;
                             * start cursor on row 5 (Exit).             */
                            ADCTracker[0] = 5;
                            drawMenu(5, 0, MENU_NOTIFICATIONS);
                            break;
                        case 1:
                            /* Exit -- end session, return to RFID scan */
                            closeActiveProfile();
                            sessionActive = 0;
                            clearMenuArea();
                            drawTextbox(4, MENU_Y + (MENU_H / 2) - FONT_H,
                                        LCD_W - 8, FONT_H * 2,
                                        "Scan RFID card", COL_FG, COL_BG);
                            break;
                        default: break;
                    }
                    break;
                default: break;
            }
            break;

        case MENU_DISPENSE:
            switch (row)
            {
                case 0:
                case 1:
                case 2:
                case 3:
                    if (dispenseMask & (1u << row))
                    {
                        /* Show "Dispensing <name> - <dose> pills..." while motor runs */
                        {
                            char scriptName[24] = {0};
                            int32_t dose = getProfileDose(row);
                            getScriptName(row, scriptName, sizeof(scriptName));
                            char msgBuf[64];
                            snprintf(msgBuf, sizeof(msgBuf),
                                     "Dispensing %s - %d pill%s...",
                                     scriptName, (int)dose,
                                     (dose == 1) ? "" : "s");
                            clearMenuArea();
                            drawTextbox(4, MENU_Y + (MENU_H / 2) - FONT_H,
                                        LCD_W - 8, FONT_H * 2,
                                        msgBuf, COL_FG, COL_BG);
                        }
                        dispense(row + 1);
                        refreshDispenseLabels();
                        drawMenu(ADCTracker[0], ADCTracker[1], MENU_DISPENSE);
                    }
                    else
                    {
                        /* trackADC holds sem -- do NOT re-acquire */
                        clearMenuArea();
                        drawTextbox(4, MENU_Y + (MENU_H / 2) - FONT_H,
                                    LCD_W - 8, FONT_H * 2,
                                    "Unauthorized", 0xF800, COL_BG);
                        usleep(1500000);
                        refreshDispenseLabels();
                        drawMenu(ADCTracker[0], ADCTracker[1], MENU_DISPENSE);
                    }
                    break;
                case 4:
                    clearMenuArea();
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
                    clearMenuArea();
                    currentMenu   = MENU_MAIN;
                    ADCTracker[0] = 0;
                    ADCTracker[1] = 0;
                    drawMenu(0, 0, MENU_MAIN);
                    break;
                default: break;
            }
            break;

        case MENU_NOTIFICATIONS:
            /* Rows 0-4 are read-only device labels; isRowSelectable() prevents
             * them from ever being selected, so only row 5 (Back) reaches here. */
            switch (row)
            {
                case 5:
                    clearMenuArea();
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
    Display_printf(display, 0, 0, "[ENTER] trackADC\n\r");
    uint16_t adcValue;
    int rows = menuDims[currentMenu].rows;
    int cols = menuDims[currentMenu].cols;

    int prevRow = ADCTracker[0];
    int prevCol = ADCTracker[1];

    uint32_t prevNow  = 0;
    uint32_t localNow = 0;

    char idxBuf[24];

    /* Draw initial footer index under sem -- bootLCD holds sem until
     * after pthread_create, so we must not touch SPI before acquiring. */
    snprintf(idxBuf, sizeof(idxBuf), "[%d,%d]", ADCTracker[0], ADCTracker[1]);
    fillRect(0, LCD_H - FOOTER_H, 250, FOOTER_H, 0x4208);
    drawTextbox(4, LCD_H - FOOTER_H + 2, 242, FOOTER_H - 4, idxBuf, 0xFFFF, 0x4208);

    while (sessionActive)
    {
        /* Read ADC before acquiring sem -- ADC is a separate bus
         * and does not need SPI protection. This frees the sem
         * for TimeKeeper_thread to draw the clock during the
         * ADC conversion window (~50ms).                        */
        extern uint16_t getADC(void);
        adcValue = getADC();

        /*
         * ADC button ranges -- values are raw ADC counts.
         * Measured button centers:
         *   Neutral = 30   Up = 200   Down = 272
         *   Right   = 414  Left = 642  Select = 871
         *
         * Each boundary is the midpoint between adjacent centers:
         *   Guard         = (30  + 200) / 2 = 115
         *   Up   / Down   = (200 + 272) / 2 = 236
         *   Down / Right  = (272 + 414) / 2 = 343
         *   Right/ Left   = (414 + 642) / 2 = 528
         *   Left / Select = (642 + 871) / 2 = 756
         *   Select upper  = 1000  (above max expected ADC noise)
         */
#define ADC_GUARD     120
#define ADC_UP_LO     ADC_GUARD
#define ADC_UP_HI     200
#define ADC_DOWN_LO   ADC_UP_HI
#define ADC_DOWN_HI   360
#define ADC_RIGHT_LO  ADC_DOWN_HI
#define ADC_RIGHT_HI  420
#define ADC_LEFT_LO   ADC_RIGHT_HI
#define ADC_LEFT_HI   580
#define ADC_SEL_LO    ADC_LEFT_HI
#define ADC_SEL_HI    850

        if (adcValue < ADC_GUARD)
        {
            usleep(10000);

            /* Update clock footer on second boundary -- only when idle
             * (no button held) so SPI is not contested mid-press.      */
            if (TimeKeeper_isValid())
            {
                prevNow  = localNow;
                localNow = TimeKeeper_getLocalTime();
                if (localNow != prevNow)
                {
                    DateTime_t  dt;
                    unixToDateTime(localNow, &dt);
                    uint8_t     hour12 = dt.hour % 12;
                    if (hour12 == 0) hour12 = 12;
                    const char *ampm = (dt.hour < 12) ? "AM" : "PM";
                    char dateBuf[12], timeBuf[12];
                    snprintf(dateBuf, sizeof(dateBuf), "%02u/%02u/%04u",
                             dt.month, dt.day, dt.year);
                    snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u:%02u %s",
                             hour12, dt.minute, dt.second, ampm);
                    drawClockFooter(dateBuf, timeBuf);
                    updateWifiIcon(wifiConnected);
                }
            }
            continue;
        }

        /* Valid button press -- process then wait for release so a held
         * button does not auto-repeat. Readings above ADC_UP_HI are
         * ignored (noise / supply transient).                          */

        if      (adcValue >= ADC_SEL_LO   && adcValue < ADC_SEL_HI)   /* Select: 115 mV */
        {
            if (isRowSelectable(currentMenu, ADCTracker[0]))
            {
                cursorSelect(currentMenu, ADCTracker[0], ADCTracker[1]);
                rows    = menuDims[currentMenu].rows;
                cols    = menuDims[currentMenu].cols;
                prevRow = ADCTracker[0];
                prevCol = ADCTracker[1];
            }
            updateWifiIcon(wifiConnected);
        }
        else if (adcValue >= ADC_DOWN_LO  && adcValue < ADC_DOWN_HI)  /* Down:   279 mV */
        {
            int next = ADCTracker[0] + 1;
            while (next < rows && !isRowSelectable(currentMenu, next)) next++;
            if (next < rows) ADCTracker[0] = next;
        }
        else if (adcValue >= ADC_RIGHT_LO && adcValue < ADC_RIGHT_HI) /* Right:  392 mV */
        {
            if (ADCTracker[1] < cols - 1) ADCTracker[1]++;
        }
        else if (adcValue >= ADC_LEFT_LO  && adcValue < ADC_LEFT_HI)  /* Left:   522 mV */
        {
            if (ADCTracker[1] > 0) ADCTracker[1]--;
        }
        else if (adcValue >= ADC_UP_LO    && adcValue < ADC_UP_HI)    /* Up:     645 mV */
        {
            int next = ADCTracker[0] - 1;
            while (next >= 0 && !isRowSelectable(currentMenu, next)) next--;
            if (next >= 0) ADCTracker[0] = next;
        }
        /* else: adcValue >= ADC_UP_HI -- out of range, ignore */

        /* Wait for button release before accepting another press */
        do { usleep(10000); adcValue = getADC(); } while (adcValue >= ADC_GUARD);

        if (ADCTracker[0] != prevRow || ADCTracker[1] != prevCol)
        {
            drawCell(prevRow, prevCol, currentMenu, false);
            drawCell(ADCTracker[0], ADCTracker[1], currentMenu, true);
            updateWifiIcon(wifiConnected);   /* refresh icon on every cursor move */

            snprintf(idxBuf, sizeof(idxBuf), "[%d,%d]", ADCTracker[0], ADCTracker[1]);
            fillRect(0, LCD_H - FOOTER_H, 250, FOOTER_H, 0x4208);
            drawTextbox(4, LCD_H - FOOTER_H + 2, 242, FOOTER_H - 4, idxBuf, 0xFFFF, 0x4208);

            prevRow = ADCTracker[0];
            prevCol = ADCTracker[1];
        }


        /* Draw clock if TimeKeeper_thread posted new time strings */
        if (g_clockDirty)
        {
            g_clockDirty = 0;
            drawClockFooter(g_clockDate, g_clockTime);
            updateWifiIcon(wifiConnected);
        }

        usleep(50000);
    }
    return NULL;
}

/* ---------------------------------------------------------------
 * initLCD_hardware()
 *
 * One-time hardware setup: SPI, interrupts, LCD init sequence,
 * and the persistent chrome (header + footer).
 * Called once from schedFuncs() at startup.
 * Draws the idle "Scan RFID card" screen when done.
 * --------------------------------------------------------------- */
void* initLCD_hardware(void* pvParameters)
{
    Display_printf(display, 0, 0, "[ENTER] initLCD_hardware\n\r");

    extern void configSPI(void);
    extern void configInterrupt(void);
    configSPI();
    setSPIRate(4000000);   /* set once -- never changed again */
    configInterrupt();
    initLCD();
    usleep(2000000);

    /* Draw persistent chrome -- header and footer stay for all screens */
    fillRect(0, 0, LCD_W, LCD_H, COL_BG);
    fillRect(0, 0, LCD_W, HEADER_H, 0x001F);
    drawTextbox(4, 4, LCD_W-8, HEADER_H-4, "EasyMedRx", 0xFFFF, 0x001F);
    fillRect(0, LCD_H-FOOTER_H, LCD_W, FOOTER_H, 0x4208);
    drawTextbox(4, LCD_H-FOOTER_H+2, LCD_W-8, FOOTER_H-4, "Status: Ready", 0xFFFF, 0x4208);

    /* Default idle screen -- shown until a card is scanned */
    clearMenuArea();
    drawTextbox(4, MENU_Y + (MENU_H / 2) - FONT_H,
                LCD_W - 8, FONT_H * 2,
                "Scan RFID card", COL_FG, COL_BG);
    updateWifiIcon(wifiConnected);


    /* LCD is ready -- now safe to start the RFID session loop */
    pthread_t          rfidThread;
    pthread_attr_t     rfidAttrs;
    struct sched_param rfidSched;
    pthread_attr_init(&rfidAttrs);
    rfidSched.sched_priority = 7;
    pthread_attr_setdetachstate(&rfidAttrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&rfidAttrs, &rfidSched);
    pthread_attr_setstacksize(&rfidAttrs, 4096);
    pthread_create(&rfidThread, &rfidAttrs, RC522_senseRFID, NULL);
    Display_printf(display, 0, 0, "\rRC522_senseRFID() scheduled: prio 7\n\r");

    return NULL;
}

/* ---------------------------------------------------------------
 * bootLCD()
 *
 * Per-session setup -- called by RC522_senseRFID() each time a
 * valid card is scanned.  Hardware is already initialised.
 * Draws the main menu and spawns trackADC() for this session.
 * When trackADC() exits (sessionActive goes 0), this session ends.
 * --------------------------------------------------------------- */
void* bootLCD(void* pvParameters)
{
    Display_printf(display, 0, 0, "[ENTER] bootLCD\n\r");

    /* Reset menu state for fresh session */
    currentMenu   = MENU_MAIN;
    ADCTracker[0] = 0;
    ADCTracker[1] = 0;

    /* Redraw chrome in case a previous session dirtied it */
    fillRect(0, 0, LCD_W, HEADER_H, 0x001F);
    drawTextbox(4, 4, LCD_W-8, HEADER_H-4, "EasyMedRx", 0xFFFF, 0x001F);
    fillRect(0, LCD_H-FOOTER_H, LCD_W, FOOTER_H, 0x4208);
    drawTextbox(4, LCD_H-FOOTER_H+2, LCD_W-8, FOOTER_H-4, "Status: Ready", 0xFFFF, 0x4208);

    /* Load logged-in user's profile so getScriptName/getProfileDose work */
    openActiveProfile();

    /* Clear menu area before drawing -- RC522_senseRFID left "Updating..." there.
     * Use clearMenuArea() rather than a raw fillRect so the wifi icon bleed
     * region (y=20..31, x=288..319) is not overwritten before drawMenu restores it. */
    clearMenuArea();

    /* Draw main menu with fresh stock labels */
    refreshDispenseLabels();
    drawMenu(0, 0, MENU_MAIN);

    pthread_t trackThread;
    pthread_attr_t trackAttrs;
    struct sched_param trackSched;
    pthread_attr_init(&trackAttrs);
    trackSched.sched_priority = 2;
    pthread_attr_setdetachstate(&trackAttrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&trackAttrs, &trackSched);
    pthread_attr_setstacksize(&trackAttrs, 2048);
    {
        int tret = pthread_create(&trackThread, &trackAttrs, trackADC, NULL);
        Display_printf(display, 0, 0, "\rtrackADC() scheduled: prio 2  ret=%d\n\r", tret);
        if (tret != 0)
            Display_printf(display, 0, 0, "trackADC create FAILED -- out of heap?\n\r");
    }

    return NULL;
}
