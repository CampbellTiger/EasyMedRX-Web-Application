#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <ti/drivers/ADC.h>
#include <ti/display/Display.h>
#include <ti/sysbios/BIOS.h>
#include <ti/drivers/GPIO.h>
#include <ti/drivers/SPI.h>                 /*SPI driver*/
#include <ti/drivers/PWM.h>
#include "ti_drivers_config.h"              /*DRIVER Configuration*/
#include <semaphore.h>

extern Display_Handle display;
extern sem_t sem;
extern PWM_Handle pwm;
extern PWM_Params params;

/*Create handle and parameters*/
SPI_Handle spi;
SPI_Params spiParams;

void* driveGPIO(void* pvParameters)
{
    sem_wait(&sem);
    while(1){
        GPIO_toggle(CONFIG_GPIO_12);
        GPIO_toggle(CONFIG_GPIO_06);
        GPIO_toggle(CONFIG_GPIO_07);
        GPIO_toggle(CONFIG_GPIO_10);
        GPIO_toggle(CONFIG_GPIO_08);
        GPIO_toggle(CONFIG_GPIO_30);
        GPIO_toggle(CONFIG_GPIO_09);
        GPIO_toggle(CONFIG_GPIO_00);
        sleep(1);
    }
    sem_post(&sem);
    return NULL;

}

void configSPI(void)
{
    SPI_Params_init(&spiParams);

    spiParams.mode = SPI_CONTROLLER;
    spiParams.bitRate = 1000000;          /*1 MHz*/
    spiParams.frameFormat = SPI_POL0_PHA0;
    spiParams.dataSize = 8;

    spi = SPI_open(CONFIG_SPI_0, &spiParams);

    if (spi == NULL) {
        while(1);   // Error trap
    }

}

void RC522_Write(uint8_t reg, uint8_t value)
{
    SPI_Transaction t;
    uint8_t tx[2];
    uint8_t rx[2];

    tx[0] = (reg << 1) & 0x7E;
    tx[1] = value;

    /*erase memory at location */
    memset(&t, 0, sizeof(t));

    t.count = 2;
    t.txBuf = tx;
    t.rxBuf = rx;

    GPIO_write(CONFIG_GPIO_08,0);
    usleep(5);
    SPI_transfer(spi, &t);
    usleep(5);
    GPIO_write(CONFIG_GPIO_08,1);
}

uint8_t RC522_Read(uint8_t reg)
{
    SPI_Transaction t;
    uint8_t tx[2];

    /*Dummy values to see if register changes over SPI communication*/
    uint8_t rx[2] = {0xAB, 0xCD};


    tx[0] = ((reg << 1) & 0x7E) | 0x80;
    tx[1] = 0x00;

    memset(&t,0,sizeof(t));
    t.count = 2;
    t.txBuf = tx;
    t.rxBuf = rx;

    GPIO_write(CONFIG_GPIO_08,0);
    usleep(5);
    SPI_transfer(spi, &t);
    usleep(5);
    GPIO_write(CONFIG_GPIO_08,1);

    return rx[1];
}

void RC522_Init()
{
    /* Soft reset */
    RC522_Write(0x01, 0x0F);
    usleep(100000);

    /* Timer configuration */
    RC522_Write(0x2A, 0x8D);
    RC522_Write(0x2B, 0x3E);
    RC522_Write(0x2D, 30);
    RC522_Write(0x2C, 0);

    /* Modulation */
    RC522_Write(0x15, 0x40);

    /* CRC preset */
    RC522_Write(0x11, 0x3D);

    /* Set antenna gain */
    RC522_Write(0x26, 0x48);

    /* Turn antenna on */
    RC522_Write(0x14, 0x03);

    Display_printf(display, 0, 0, "RC522 RFID initialization complete\r\n");
}

int RC522_REQA(void)
{
    RC522_Write(0x01,0x00);   /* Idle */
    RC522_Write(0x04,0x7F);   /* Clear IRQ */
    RC522_Write(0x0A,0x80);   /* Flush FIFO */
    RC522_Write(0x09,0x26);   /* REQA */
    RC522_Write(0x0D,0x07);   /* 7-bit frame */
    RC522_Write(0x01,0x0C);   /* Transceive */

    uint8_t framing = RC522_Read(0x0D);
    RC522_Write(0x0D, framing | 0x80);  // Start transmission

    /* Wait for response */
    uint8_t irq;
    Display_printf(display, 0, 0, "Entering REQA Loop\r\n");
    do {
        irq = RC522_Read(0x04);
    } while(!(irq & 0x30));

    return 1;   // card detected
}

void RC522_UID(void)
{
    RC522_Write(0x01,0x00);  // Idle
    RC522_Write(0x0A,0x80);  // Flush FIFO
    RC522_Write(0x09,0x93);
    RC522_Write(0x09,0x20);
    RC522_Write(0x0D,0x00);
    RC522_Write(0x01,0x0C);  // Transceive

    uint8_t framing = RC522_Read(0x0D);
    RC522_Write(0x0D, framing | 0x80);

    /* wait for response */
    uint8_t irq;
    Display_printf(display, 0, 0, "Entering UID Loop\r\n");
    do {
        irq = RC522_Read(0x04);
    } while(!(irq & 0x30));
}

void RC522_ReadFIFO(uint8_t *uid)
{

    uint8_t length = RC522_Read(0x0A);

    Display_printf(display, 0, 0, "Entering FIF0 Loop\r\n");
    for(int i=0;i<length;i++)
    {
        uid[i] = RC522_Read(0x09);
    }
}

void* RC522_senseRFID(void* pvParameter)
{
    uint8_t uid[10];
    configSPI();
    RC522_Init();

    /* Asking for version determines if SPI is working */
    uint8_t version = RC522_Read(0x37);
    Display_printf(display, 0, 0, "RC522 Version: 0x%02X\r\n", version);

    while(1)
    {
        sem_wait(&sem);

        if(RC522_REQA())
        {
            RC522_UID();
            RC522_ReadFIFO(uid);

            Display_printf(display,0,0, "RFID: %02X %02X %02X %02X",
                uid[0],uid[1],uid[2],uid[3]);
        }

        sem_post(&sem);
        sleep(1);
    }

    return NULL;
}

void toggleLCD(void* pvParameters)
{
    GPIO_toggle(CONFIG_GPIO_09);
}

void toggleLCD_RST(void* pvParameters)
{
    GPIO_toggle(CONFIG_GPIO_30);
}

/*RC522 RFID SPI
 * Bit7 = r/w, Bit6-1 = address, Bit0 = 0
 * */

// Task function to read ADC
void* getADC(void *pvParameters)
{
    sem_wait(&sem);

    ADC_Handle   adc;
    ADC_Params   params;
    uint16_t     adcValue;
    uint32_t     adcMicroVolt;

    ADC_Params_init(&params);
    adc = ADC_open(CONFIG_ADC_0, &params); // Open ADC channel

    if (adc == NULL) {
        while (1); // Error
    }
    if (ADC_convert(adc, &adcValue) == ADC_STATUS_SUCCESS) {
        Display_printf(display, 0, 0, "ADC Value = %d \r\n",adcValue);
        adcMicroVolt = ADC_convertRawToMicroVolts(adc, adcValue);
        Display_printf(display, 0, 0, "Voltage = %duV \r\n",adcMicroVolt);
    }
    else{
        Display_printf(display, 0, 0, "ADC error\r\n");
    }

    ADC_close(adc);
    sem_post(&sem);
    return NULL;
}

void* startPWM(void){

    pwm = PWM_open(CONFIG_PWM_0, &params);
    if (pwm == NULL) {
        while(1);
    }
    PWM_start(pwm);
    Display_printf(display, 0, 0, "PWM started\r\n");
    return NULL;
}

void* setPWM(int uS){
    params.idleLevel      = PWM_IDLE_LOW;
    params.periodUnits    = PWM_PERIOD_US;                  /* change period to uS*/
    params.periodValue    = 20000;                          /* 20ms = 50 Hz */
    params.dutyUnits      = PWM_DUTY_US;                    /* time high per period = 1 */
    params.dutyValue      = 1400;                           /* time set high per cycle */
    Display_printf(display, 0, 0, "PWM set to %d uS high per cycle \r\n", params.dutyValue);
    return NULL;
}

/* PWM Notes:
   setPWM(1200);  low CW
   setPWM(1000);  faster CW
   setPWM(900);   max CW

   setPWM(1700);  slow CCW
   setPWM(1900);  faster CCW
   setPWM(2100);  max CCW */

/*
 * GPIO_write(LCD_CS, 0);      // Set CS active low
 * SPI_transfer(spi, &transaction);
 * GPIO_write(LCD_CS, 1);      // Set CS high
 */
