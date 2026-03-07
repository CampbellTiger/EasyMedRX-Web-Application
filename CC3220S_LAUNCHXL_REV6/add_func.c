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
extern void* driveGPIO(void *pvParameters);
extern void* jsonThread(void *arg);
extern void* getADC(void *pvParameters);
extern void* configSPI(void* pvParameters);
extern void* transferSPI(void* pvParameters);
extern void* startPWM(void);
extern void* setPWM(int uS);
extern void* RC522_senseRFID(void* pvParameter);

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

    /*Print Console*/
/*  pthread_t printThread;
    pthread_attr_t printAttrs;
    struct sched_param printSched;
    pthread_attr_init(&printAttrs);
    printSched.sched_priority = 2;
    pthread_attr_setdetachstate(&printAttrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&printAttrs, &printSched);
    pthread_attr_setstacksize(&printAttrs, 4096);
    pthread_create(&printThread, &printAttrs, printConsole, "Hello world");
    Display_printf(display, 0, 0,"\rprintConsole() scheduled: prio 2\n\r");
*/

    /*Drive GPIO*/
/*    pthread_t driveThread;
    pthread_attr_t driveAttrs;
    struct sched_param driveSched;
    pthread_attr_init(&driveAttrs);
    driveSched.sched_priority = 4;
    pthread_attr_setdetachstate(&driveAttrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&driveAttrs, &driveSched);
    pthread_attr_setstacksize(&driveAttrs, 4096);
    pthread_create(&driveThread, &driveAttrs, driveGPIO, NULL);
    Display_printf(display, 0, 0,"\rdriveGPIO() scheduled: prio 4\n\r");
*/

/*
    pthread_t buttonThread;
    pthread_attr_t buttonAttr;
    struct sched_param buttonSched;
    pthread_attr_init(&buttonAttr);
    buttonSched.sched_priority = 5;
    pthread_attr_setdetachstate(&buttonAttr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&buttonAttr, &buttonSched);
    pthread_attr_setstacksize(&buttonAttr, 4096);
    pthread_create(&buttonThread, &buttonAttr, getADC, NULL);
    Display_printf(display, 0, 0,"\rADC() scheduled: prio 5\n\r");
*/

/*
    pthread_t confSPI;
    pthread_attr_t confSPIAttr;
    struct sched_param confSPISched;
    pthread_attr_init(&confSPIAttr);
    confSPISched.sched_priority = 6;
    pthread_attr_setdetachstate(&confSPIAttr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&confSPIAttr, &confSPISched);
    pthread_attr_setstacksize(&confSPIAttr, 4096);
    pthread_create(&confSPI, &confSPIAttr, configSPI, NULL);
    Display_printf(display, 0, 0,"\rconfSPI() scheduled: prio 5\n\r");
*/
/*
    pthread_t tranSPI;
    pthread_attr_t tranSPIAttr;
    struct sched_param tranSPISched;
    pthread_attr_init(&tranSPIAttr);
    tranSPISched.sched_priority = 7;
    pthread_attr_setdetachstate(&tranSPIAttr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&tranSPIAttr, &tranSPISched);
    pthread_attr_setstacksize(&tranSPIAttr, 4096);
    pthread_create(&tranSPI, &tranSPIAttr, transferSPI, NULL);
    Display_printf(display, 0, 0,"\rtransSPI() scheduled: prio 7\n\r");
*/

    /*jsonThread*/
    /*
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
    Display_printf(display, 0, 0,"\rAll threads scheduled: initialization successful\n\r");
    */

    pthread_t RFIDThread;
    pthread_attr_t RFIDAttrs;
    struct sched_param RFIDSched;
    pthread_attr_init(&RFIDAttrs);
    RFIDSched.sched_priority = 9;
    pthread_attr_setdetachstate(&RFIDAttrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&RFIDAttrs, &RFIDSched);
    pthread_attr_setstacksize(&RFIDAttrs, 4096);
    pthread_create(&RFIDThread, &RFIDAttrs, RC522_senseRFID, NULL);
    Display_printf(display, 0, 0,"\rjsonThread() scheduled: prio 7\n\r");
    Display_printf(display, 0, 0,"\rAll threads scheduled: initialization successful\n\r");

    /*setPWM(1200);
    startPWM();*/
    sem_post(&sem);

    return NULL;
}


