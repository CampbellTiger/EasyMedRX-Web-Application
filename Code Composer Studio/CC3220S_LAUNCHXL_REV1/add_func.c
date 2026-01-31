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

/* External Functions */
extern void* driveGPIO(void* pvParameters);

/* Fuction creation */
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

/* Fuction scheduling */
void* schedFuncs(void* pvParameters)
{

    /*Print Console*/

    pthread_t printThread;
    pthread_attr_t printAttrs;
    struct sched_param printSched;
    pthread_attr_init(&printAttrs);
    printSched.sched_priority = 2;
    pthread_attr_setdetachstate(&printAttrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&printAttrs, &printSched);
    pthread_attr_setstacksize(&printAttrs, 4096);
    pthread_create(&printThread, &printAttrs, printConsole, "Hello world");

    /*Drive GPIO*/
    pthread_t driveThread;
    pthread_attr_t driveAttrs;
    struct sched_param driveSched;
    pthread_attr_init(&driveAttrs);
    driveSched.sched_priority = 4;
    pthread_attr_setdetachstate(&driveAttrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&driveAttrs, &driveSched);
    pthread_attr_setstacksize(&driveAttrs, 4096);
    pthread_create(&driveThread, &driveAttrs, driveGPIO, NULL);
    return NULL;
}


