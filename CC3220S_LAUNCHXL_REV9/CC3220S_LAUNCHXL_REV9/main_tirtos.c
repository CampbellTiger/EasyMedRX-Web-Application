/*
 *  ======== main_tirtos.c ========
 */
#include <stdint.h>

/* POSIX Header files */
#include <pthread.h>

/* RTOS header files */
#include <ti/sysbios/BIOS.h>

/* TI-DRIVERS Header files */
#include "ti_drivers_config.h"

/* TI-RTOS Header files */
#include <ti/drivers/GPIO.h>

/* Terminal Display Header file */
#include <ti/display/Display.h>

/* SPI Header file */
#include <ti/drivers/SPI.h>

/* Semaphore Header File */
#include <semaphore.h>

/* ADC header file */
#include <ti/drivers/ADC.h>

/* SPI header file */
#include <ti/drivers/SPI.h>

/* PWM header file */
#include <ti/drivers/PWM.h>

extern int jsonInit(void);
extern void* mainThread(void *arg0);
extern void* schedFuncs(void *arg0);

/* Stack size in bytes */
#define THREADSTACKSIZE    4096

/*PWM handle Creation*/
PWM_Handle pwm;
PWM_Params params;

/* Global Semaphore creation */
sem_t sem;

/*
 *  ======== main ========
 */
int main(void)
{
    pthread_t thread;
    pthread_attr_t pAttrs;

    pthread_t funcThread;
    pthread_attr_t funcAttrs;

    struct sched_param priParam;
    struct sched_param funcParam;

    int retc;
    int detachState;

    /* Call init functions */
    Board_init();
    Display_init();
    SPI_init();
    ADC_init();
    detachState = PTHREAD_CREATE_DETACHED;

    /* Semaphore Initialization */
    sem_init(&sem, 0, 0);

    /* PWM initialization*/
    PWM_init();
    PWM_Params_init(&params);

    /*mainThread Scheduling*/
    pthread_attr_init(&pAttrs);
    priParam.sched_priority = 3;
    retc = pthread_attr_setdetachstate(&pAttrs, detachState);
    pthread_attr_setschedparam(&pAttrs, &priParam);
    retc |= pthread_attr_setstacksize(&pAttrs, THREADSTACKSIZE);
    retc = pthread_create(&thread, &pAttrs, mainThread, NULL);

    /* schedFuncts Scheduling */
    pthread_attr_init(&funcAttrs);
    funcParam.sched_priority = 1;
    retc = pthread_attr_setdetachstate(&funcAttrs, detachState);
    pthread_attr_setschedparam(&funcAttrs, &funcParam);
    retc |= pthread_attr_setstacksize(&funcAttrs, THREADSTACKSIZE);
    retc = pthread_create(&funcThread, &funcAttrs, schedFuncs, NULL);

    BIOS_start();

    return (0);
}

/*PTHREAD INITIALIZATION AND SCHEDULING*/
/* #include <pthread.h> */
/* #include <sched.h> */

/* pthread_t thread; */                                                    /*declare thread*/
/* pthread_attr_t attrs; */                                                /*declare thread attributes*/
/* struct sched_param sched; */                                            /*create scheduling parameter struct*/

/* pthread_attr_init(&attrs); */                                           /*initialize parameter attributes*/

/* sched.sched_priority = 9; */                                             /*priority spans 1-15*/
/* pthread_attr_setschedparam(&attrs, &sched); */
/* pthread_attr_setstacksize(&attrs, 4096); */                              /*stack size*/

/* pthread_create(&thread, &attrs, threadFunc, NULL); */                    /*create thread*/

/*Semaphore required headers and declarations*/
/* #include <semaphore.h>*/
/*Declare sem*/
/* int sem_t sem; */

/*Initialize sem*/
/*sem_init(sem_t *sem, int pshared, unsigned int value)*/

/*wait by blocking*/
/* sem_wait(sem_t *sem) */     /*blocks if value == 0 */

/*Timed wait(timeout)*/
/*sem_timedwait(sem_t *sem, const struct timespec *abs_timeout) */

/*Signal a release*/
/* sem_post(sem_t *sem) */

/*destoy sem*/
/* sem_destroy(sem_t *sem) */

/*Mutex(mutual exclusion)*/
/*locks a resource so only one task can access it at a time*/
/*prevents multiple threads from simultaneously modifying shared data*/





