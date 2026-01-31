#include <pthread.h>
#include <ti/sysbios/BIOS.h>
#include <ti/drivers/GPIO.h>
#include <ti/drivers/SPI.h>                 /*SPI driver*/
#include "ti_drivers_config.h"              /*DRIVER Configuration*/
#include <unistd.h>

void* driveGPIO(void* pvParameters)

{
    while(1){
        GPIO_toggle(CONFIG_GPIO_LED_0);
        sleep(1);
    }
        return NULL;

}




/*GP05 = SCLK*/
/*GP06 = MISO*/
/*GP07 = MOSI*/
/*GP08 = CS*/

/*SPI EXAMPLE CODE*/
/* SPI_Handle handle; */                                /* pointer to a specific SPI peripheral */
/* SPI_Transaction trans; */                            /* Structure that handles 1 SPI transaction */
/* SPI_init(); */                                       /* SPI_init() already called in mainThread() */
/* handle = SPI_open(CONFIG_SPI_0, &params); */         /* SPI_open() returns a handle if successful */
/* trans.txBuf = txBuf; */                              /* pointer to data that will be sent*/
/* trans.rxBuf = rxBuf; */                              /* pointer to where recieved data will be stored */
/* trans.count = 4; */                                  /* the number of bytes that will be sent over one transaction*/
/* SPI_transfer(handle, &trans); */                     /* perform the transfer, automatically toggles CS */

/* 3 Wire SPI Notes */
/* MOSI/MISO combined is SDIO, direction of data must be switched */
/* Uses SCLK, SDIO, and CS */
/* .pinMode = SPI_3PIN_MODE*/

/* 3 Wire SPI EXAMPLE CODE */
/* SPI_Handle handle; */
/* SPI_Transaction trans; */
/* SPI_init(); */
/* handle = SPI_open(CONFIG_SPI_0, &params); */
/* trans.txBuf = txBuf; */
/* trans.rxBuf = rxBuf; */
/* trans.count = 4; */
/* SPI_transfer(handle, &trans); */
