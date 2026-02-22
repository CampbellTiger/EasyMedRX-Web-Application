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


extern void * mainThread(void *arg0);
extern void * schedFuncs(void *arg0);

/* Stack size in bytes */
#define THREADSTACKSIZE    4096

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

    /* Call board init functions */
    Board_init();

    /* Set priority and stack size attributes */
    pthread_attr_init(&pAttrs);
    priParam.sched_priority = 3;

    pthread_attr_init(&funcAttrs);
    funcParam.sched_priority = 1;

    detachState = PTHREAD_CREATE_DETACHED;
    retc = pthread_attr_setdetachstate(&pAttrs, detachState);

    if(retc != 0)
    {
        /* pthread_attr_setdetachstate() failed */
        while(1)
        {
            ;
        }
    }

    retc = pthread_attr_setdetachstate(&funcAttrs, detachState);

    if(retc != 0)
    {
        /* pthread_attr_setdetachstate() failed */
        while(1)
        {
            ;
        }
    }

    pthread_attr_setschedparam(&pAttrs, &priParam);
    retc |= pthread_attr_setstacksize(&pAttrs, THREADSTACKSIZE);

    if(retc != 0)
    {
        /* pthread_attr_setstacksize() failed */
        while(1)
        {
            ;
        }
    }

    pthread_attr_setschedparam(&funcAttrs, &funcParam);
    retc |= pthread_attr_setstacksize(&funcAttrs, THREADSTACKSIZE);

    if(retc != 0)
    {
        /* pthread_attr_setstacksize() failed */
        while(1)
        {
            ;
        }
    }

    retc = pthread_create(&thread, &pAttrs, mainThread, NULL);
    if(retc != 0)
    {
        /* pthread_create() failed */
        while(1)
        {
            ;
        }
    }

    retc = pthread_create(&funcThread, &funcAttrs, schedFuncs, NULL);
    if(retc != 0)
    {
        /* pthread_create() failed */
        while(1)
        {
            ;
        }
    }

    BIOS_start();

    return (0);
}

/*
 *  ======== dummyOutput ========
 *  Dummy SysMin output function needed for benchmarks and size comparison
 *  of FreeRTOS and TI-RTOS solutions.
 */

void dummyOutput(void)
{
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
/* sem_t sem;*/

/* sem_init(&sem, 0, 0); */  /*pshared=0, initial count=0*/

/*wait by blocking*/
/* sem_wait(&sem);*/      /*blocks if value == 0 */

/*trywait by nonblocking*/
/* if (sem_trywait(&sem) == 0) {aquired} */

/*Timed wait(timout)*/
/* sem_timedwait(&sem, &timeout);*/

/*Signal a relese*/
/* sem_post(&sem); */



/*Mutex(mutual exclusion)*/
/*locks a resource so only one task can access it at a time*/
/*prevents multiple threads from simultaneously modifying shared data*/


