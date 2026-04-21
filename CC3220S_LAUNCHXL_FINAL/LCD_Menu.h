#ifndef LCD_MENU_H
#define LCD_MENU_H

#include <stdint.h>
#include <stdbool.h>

/* LCD Font Data
 * The Adafruit 5x7 font stores 8 bits per column: bits 0-6 are the normal
 * glyph body, bit 7 is the descender row used by g, j, p, q, y, etc.
 * FONT_H must be 8 so drawChar() renders all 8 rows and descenders appear. */
#define FONT_W          5
#define FONT_H          8
#define FONT_GAP        1
#define CHAR_W          (FONT_W + FONT_GAP)

/* LCD display geometry */
#define LCD_W           320
#define LCD_H           240
#define HEADER_H        20
#define FOOTER_H        20
#define MENU_Y          HEADER_H
#define MENU_H          (LCD_H - HEADER_H - FOOTER_H)

/* LCD Color Data */
#define COL_BG          0x0000
#define COL_FG          0xFFFF
#define COL_SEL_BG      0x001F
#define COL_SEL_FG      0xFFFF

/* Highlight padding around text in pixels */
#define HIGHLIGHT_PAD   3

typedef enum
{
    MENU_MAIN          = 0,
    MENU_DISPENSE,
    MENU_ACCOUNT,
    MENU_NOTIFICATIONS
} MENU_State;

/* Shared navigation state defined in LCD_Menu.c, accessible elsewhere */
extern MENU_State currentMenu;
extern int        ADCTracker[2];

/* Menu area helpers */
void     clearMenuArea(void);   /* clears menu zone, preserves wifi icon bleed */

/* LCD primitives */
void     writeCmd(uint8_t cmd);
void     writeData(uint8_t data);
void     writeDataBulk(uint8_t *buf, size_t len);
void     initLCD(void);
void     setAddrWindow(int x1, int y1, int x2, int y2);
void     sendByte(uint8_t byte);
void     fillRect(int x, int y, int w, int h, uint16_t color);
void     redTest(void);
void     drawChar(int x, int y, char c, uint16_t fg, uint16_t bg);
int      drawTextbox(int x, int y, int maxW, int maxH, const char *str, uint16_t fg, uint16_t bg);

/* Menu */
void     drawMenu(int selRow, int selCol, MENU_State menu);
void     cursorSelect(MENU_State menu, int row, int col);

/* Wi-Fi icon
 * Pass 1 after a successful HTTP POST, 0 when the server is unreachable.
 * Updates the internal wifiConnected state and redraws the header icon. */
void     updateWifiIcon(uint8_t connected);

/* Clock footer -- drawn by trackADC when g_clockDirty is set */
void     drawClockFooter(const char *dateLine, const char *timeLine);

/* Shared LCD state -- defined in LCD_Menu.c */
extern volatile uint8_t wifiConnected;  /* 0 = no wifi, 1 = connected         */

/* Shared clock buffer -- written by TimeKeeper_thread, drawn by trackADC */
extern volatile uint8_t g_clockDirty;
extern char             g_clockDate[12];
extern char             g_clockTime[12];

/* Threads */
void    *trackADC(void *pvParameters);
void    *initLCD_hardware(void *pvParameters);
void    *bootLCD(void *pvParameters);

#endif /* LCD_MENU_H */
