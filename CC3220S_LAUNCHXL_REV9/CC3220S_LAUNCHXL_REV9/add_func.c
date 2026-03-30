/*
 *          ======== add_func.c ========
 *  add useful/needed functions to schedule here
 */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

/* POSIX Header files */
#include <pthread.h>
#include <sched.h>
/* RTOS header files */
#include <ti/sysbios/BIOS.h>

/* TI-DRIVERS Header files */
#include "ti_drivers_config.h"

/* TI-RTOS Header files */
#include <ti/drivers/GPIO.h>

/* Semaphore Header files */
#include <semaphore.h>

/* Display Header files */
#include <ti/display/Display.h>

/* External Functions */
#include "json_make.h"
#include "LCD_Menu.h"
#include "drive_gpio.h"

/*Display handle*/
extern Display_Handle display;

/* External objects */
extern sem_t sem;

/* Function creation */
void* printConsole(void* arg0)
{
    char* mes = (char*)arg0;
    while(1)
    {
        printf("%s\n",mes);
        sleep(3);
    }
    return NULL;
}

/* Function scheduling */
void* schedFuncs(void* pvParameters)
{
    sem_wait(&sem);

    /*Test SPI*/
    pthread_t SPIThread;
    pthread_attr_t SPIAttrs;
    struct sched_param SPISched;
    pthread_attr_init(&SPIAttrs);
    SPISched.sched_priority = 2;
    pthread_attr_setdetachstate(&SPIAttrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&SPIAttrs, &SPISched);
    pthread_attr_setstacksize(&SPIAttrs, 4096);
    pthread_create(&SPIThread, &SPIAttrs, testSPI, NULL);
    Display_printf(display, 0, 0,"\rtestSPI() scheduled: prio 2\n\r");

    /*Drive GPIO*/
/*  pthread_t driveThread;
    pthread_attr_t driveAttrs;
    struct sched_param driveSched;
    pthread_attr_init(&driveAttrs);
    driveSched.sched_priority = 4;
    pthread_attr_setdetachstate(&driveAttrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&driveAttrs, &driveSched);
    pthread_attr_setstacksize(&driveAttrs, 4096);
    pthread_create(&driveThread, &driveAttrs, driveGPIO, NULL);
    Display_printf(display, 0, 0,"\rdriveGPIO() scheduled: prio 4\n\r");*/

    /*jsonThread*/
    pthread_t jsonT;
    pthread_attr_t jsonAttrs;
    struct sched_param jsonSched;
    pthread_attr_init(&jsonAttrs);
    jsonSched.sched_priority = 6;
    pthread_attr_setdetachstate(&jsonAttrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&jsonAttrs, &jsonSched);
    pthread_attr_setstacksize(&jsonAttrs, 4096);
    pthread_create(&jsonT, &jsonAttrs, jsonThread, NULL);
    Display_printf(display, 0, 0,"\rjsonThread() scheduled: prio 7\n\r");


    /*pthread_t LCDThread;
    pthread_attr_t LCDAttrs;
    struct sched_param LCDSched;
    pthread_attr_init(&LCDAttrs);
    LCDSched.sched_priority = 9;
    pthread_attr_setdetachstate(&LCDAttrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&LCDAttrs, &LCDSched);
    pthread_attr_setstacksize(&LCDAttrs, 4096);
    pthread_create(&LCDThread, &LCDAttrs, bootLCD, NULL);
    Display_printf(display, 0, 0,"\rbootLCD() scheduled: prio 9\n\r");*/

    /*pthread_t demuxThread;
    pthread_attr_t demuxAttrs;
    struct sched_param demuxSched;
    pthread_attr_init(&demuxAttrs);
    demuxSched.sched_priority = 9;
    pthread_attr_setdetachstate(&demuxAttrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&demuxAttrs, &demuxSched);
    pthread_attr_setstacksize(&demuxAttrs, 4096);
    pthread_create(&demuxThread, &demuxAttrs, testDemux, NULL);
    Display_printf(display, 0, 0,"\testDemux() scheduled: prio 9\n\r");
    Display_printf(display, 0, 0,"\rAll threads scheduled: initialization successful\n\r");*/


    /*createConsoleTask();
    setPWM(1200);
    startPWM();*/
    sem_post(&sem);

    return NULL;
}
