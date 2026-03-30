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
#include "LCD_Menu.h"

/* RC522 registers */
#define CommandReg      0x01
#define ComIEnReg       0x02
#define ComIrqReg       0x04
#define ErrorReg        0x06
#define Status2Reg      0x08
#define FIFODataReg     0x09
#define FIFOLevelReg    0x0A
#define ControlReg      0x0C
#define BitFramingReg   0x0D
#define CollReg         0x0E
#define ModeReg         0x11
#define TxModeReg       0x12
#define RxModeReg       0x13
#define TxControlReg    0x14
#define TxASKReg        0x15
#define TModeReg        0x2A
#define TPrescalerReg   0x2B
#define TReloadRegH     0x2C
#define TReloadRegL     0x2D
#define RFCfgReg        0x26
#define VersionReg      0x37

#define PCD_Idle        0x00
#define PCD_Transceive  0x0C
#define PCD_SoftReset   0x0F

typedef enum
{
    RC522_OK = 0,
    RC522_ERR,
    RC522_TIMEOUT,
    RC522_COLLISION
} RC522_Status;

extern Display_Handle display;
extern sem_t          sem;
extern PWM_Handle     pwm;
extern PWM_Params     params;

SPI_Handle  spi;
SPI_Params  spiParams;
ADC_Handle  adc;
ADC_Params  adcParams;

static volatile int dispenseTracker = 0;

/* ---------------------------------------------------------------
 * Interrupt
 * --------------------------------------------------------------- */
void IRQHandler(uint_least8_t index)
{
    if (dispenseTracker > 0)
        dispenseTracker = 0;
    sem_post(&sem);
}

void configInterrupt(void)
{
    GPIO_setCallback(CONFIG_GPIO_11, IRQHandler);
    GPIO_enableInt(CONFIG_GPIO_11);
}

/* ---------------------------------------------------------------
 * SPI config
 * --------------------------------------------------------------- */
void configSPI(void)
{
    SPI_Params_init(&spiParams);
    spiParams.mode        = SPI_CONTROLLER;
    spiParams.bitRate     = 4000000;
    spiParams.frameFormat = SPI_POL0_PHA0;
    spiParams.dataSize    = 8;
    spi = SPI_open(CONFIG_SPI_0, &spiParams);
    if (spi == NULL) { while(1); }
}

void* testSPI(void* pvParameters)
{
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

        /*usleep(500000);*/
        if (!ok)
        {
            Display_printf(display, 0, 0, "SPI transfer failed");
            return NULL;
        }
        Display_printf(display, 0, 0, "Tx[0]=%02x Tx[1]=%02x", tx[0], tx[1]);
        Display_printf(display, 0, 0, "Rx[0]=%02x Rx[1]=%02x", rx[0], rx[1]);
    }
    return NULL;
}

/* ---------------------------------------------------------------
 * RFID
 * --------------------------------------------------------------- */
void RC522_Write(uint8_t reg, uint8_t value)
{
    SPI_Transaction t;
    uint8_t tx[2] = { (reg << 1) & 0x7E, value };
    uint8_t rx[2];
    memset(&t, 0, sizeof(t));
    t.count = 2; t.txBuf = tx; t.rxBuf = rx;
    GPIO_write(CONFIG_GPIO_08, 0); usleep(5);
    bool ok = SPI_transfer(spi, &t);
    usleep(5); GPIO_write(CONFIG_GPIO_08, 1); usleep(5);
    if (!ok) Display_printf(display, 0, 0, "SPI transfer failed");
}

uint8_t RC522_Read(uint8_t reg)
{
    SPI_Transaction t;
    uint8_t tx[2] = { ((reg << 1) & 0x7E) | 0x80, 0x00 };
    uint8_t rx[2] = {0xAB, 0xCD};
    memset(&t, 0, sizeof(t));
    t.count = 2; t.txBuf = tx; t.rxBuf = rx;
    GPIO_write(CONFIG_GPIO_08, 0); usleep(5);
    bool ok = SPI_transfer(spi, &t);
    usleep(5); GPIO_write(CONFIG_GPIO_08, 1); usleep(5);
    if (!ok) { Display_printf(display, 0, 0, "SPI transfer failed"); return 0xFF; }
    return rx[1];
}

static void RC522_SetBitMask(uint8_t reg, uint8_t mask)
{
    RC522_Write(reg, RC522_Read(reg) | mask);
}

static void RC522_AntennaOn(void)
{
    uint8_t v = RC522_Read(TxControlReg);
    if ((v & 0x03U) != 0x03U) RC522_Write(TxControlReg, v | 0x03U);
}

int RC522_Init_CC3220(void)
{
    GPIO_write(CONFIG_GPIO_08, 1);
    GPIO_write(CONFIG_GPIO_30, 1);
    usleep(10000);
    RC522_Write(CommandReg, PCD_SoftReset);
    usleep(50000);
    uint8_t version = RC522_Read(VersionReg);
    Display_printf(display, 0, 0, "Version: %02x", version);
    RC522_Write(TModeReg,      0x80);
    RC522_Write(TPrescalerReg, 0xA9);
    RC522_Write(TReloadRegH,   0x03);
    RC522_Write(TReloadRegL,   0xE8);
    RC522_Write(TxASKReg,      0x40);
    RC522_Write(ModeReg,       0x3D);
    RC522_Write(RFCfgReg,      0x48);
    RC522_AntennaOn();
    return 0;
}

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
    if (timeout <= 0) return RC522_TIMEOUT;
    uint8_t err = RC522_Read(ErrorReg);
    if (err & 0x13) return RC522_ERR;
    if (err & 0x08) return RC522_COLLISION;
    uint8_t n = RC522_Read(FIFOLevelReg);
    if (n > *backLen) return RC522_ERR;
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
    uint8_t cmd[2] = {0x93, 0x20}, resp[5] = {0}, len = sizeof(resp);
    RC522_Status st = RC522_Transceive(cmd, 2, resp, &len, 0, NULL);
    if (st != RC522_OK || len != 5) return RC522_ERR;
    if ((resp[0]^resp[1]^resp[2]^resp[3]) != resp[4]) return RC522_ERR;
    uid[0]=resp[0]; uid[1]=resp[1]; uid[2]=resp[2]; uid[3]=resp[3];
    if (bcc) *bcc = resp[4];
    return RC522_OK;
}

void RC522_senseRFID(void)
{
    if (RC522_Init_CC3220() != 0) { Display_printf(display,0,0,"RC522 init failed"); return; }
    uint8_t atqa[2], uid[4], bcc;
    while(1) {
        sem_wait(&sem);
        if (RC522_RequestA(atqa)==RC522_OK && RC522_Anticoll_CL1(uid,&bcc)==RC522_OK)
            Display_printf(display,0,0,"UID: %02x %02x %02x %02x",uid[0],uid[1],uid[2],uid[3]);
        sem_post(&sem);
        usleep(50000);
    }
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
void setPWM(int uS)
{
    if (pwm != NULL) PWM_stop(pwm);
    params.idleLevel   = PWM_IDLE_LOW;
    params.periodUnits = PWM_PERIOD_US;
    params.periodValue = 20000;
    params.dutyUnits   = PWM_DUTY_US;
    params.dutyValue   = uS;
    pwm = PWM_open(CONFIG_PWM_0, &params);
    if (pwm == NULL) { while(1); }
    PWM_start(pwm);
}

void* testDemux(void* pvParameters)
{
    sem_wait(&sem);
    setPWM(10000);
    while(1)
    {
        Display_printf(display, 0, 0, "Servo1, IR1");
        GPIO_write(CONFIG_GPIO_07, 0); GPIO_write(CONFIG_GPIO_10, 0);
        GPIO_write(CONFIG_GPIO_12, 0); GPIO_write(CONFIG_GPIO_06, 0);
        sleep(10);

        Display_printf(display, 0, 0, "Servo2, IR2");
        GPIO_write(CONFIG_GPIO_07, 1); GPIO_write(CONFIG_GPIO_10, 0);
        GPIO_write(CONFIG_GPIO_12, 1); GPIO_write(CONFIG_GPIO_06, 0);
        sleep(10);

        Display_printf(display, 0, 0, "Servo3, IR3");
        GPIO_write(CONFIG_GPIO_07, 0); GPIO_write(CONFIG_GPIO_10, 1);
        GPIO_write(CONFIG_GPIO_12, 0); GPIO_write(CONFIG_GPIO_06, 1);
        sleep(10);

        Display_printf(display, 0, 0, "Servo4, IR4");
        GPIO_write(CONFIG_GPIO_07, 1); GPIO_write(CONFIG_GPIO_10, 1);
        GPIO_write(CONFIG_GPIO_12, 1); GPIO_write(CONFIG_GPIO_06, 1);
        sleep(10);
    }
    sem_post(&sem);
    return NULL;
}

/* ---------------------------------------------------------------
 * Dispense
 * --------------------------------------------------------------- */
int readIR(int container)
{
    switch (container)
    {
        case 1: GPIO_write(CONFIG_GPIO_07, 0); GPIO_write(CONFIG_GPIO_10, 0); break;
        case 2: GPIO_write(CONFIG_GPIO_07, 1); GPIO_write(CONFIG_GPIO_10, 0); break;
        case 3: GPIO_write(CONFIG_GPIO_07, 0); GPIO_write(CONFIG_GPIO_10, 1); break;
        case 4: GPIO_write(CONFIG_GPIO_07, 1); GPIO_write(CONFIG_GPIO_10, 1); break;
        default: return -1;
    }

    dispenseTracker = container;

    if (dispenseTracker != 0)
    {
        sem_wait(&sem);
        if (dispenseTracker != 0) return -1;
    }
    return 0;
}

int dispense(int container)
{
    setPWM(1400);

    int result = readIR(container);
    if (result != 0)
    {
        if (pwm != NULL) PWM_stop(pwm);
        fillRect(0, MENU_Y, LCD_W, MENU_H, COL_BG);
        drawTextbox(4, MENU_Y + 4, LCD_W - 8, MENU_H - 8,
                    "Dispense failed!", 0xF800, COL_BG);
        return -1;
    }

    if (pwm != NULL) PWM_stop(pwm);
    return 0;
}

