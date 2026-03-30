#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>     // atoi, strtol

/* TI-RTOS7 */
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Semaphore.h>

/* ======== Configuration ======== */
#define CONSOLE_TASK_STACK_SIZE  1024
#define CONSOLE_TASK_PRIORITY    1
#define CONSOLE_INPUT_MAX        64
#define CONSOLE_SLEEP_MS         10
#define CONSOLE_SEM_TIMEOUT_MS   5000   // 5 second semaphore

/* Convert milliseconds to RTOS ticks */
#define MS_TO_TICKS(ms)  ((ms) * (1000 / Clock_tickPeriod))

/* ======== Task ======== */
Task_Handle  consoleTaskHandle;
Task_Struct  consoleTaskStruct;
uint8_t      consoleTaskStack[CONSOLE_TASK_STACK_SIZE];

/* ======== Console Semaphore ======== */
/* Signals when user input is ready to be processed */
Semaphore_Handle consoleSemHandle;
Semaphore_Struct consoleSemStruct;

/* ======== Console Mutex ======== */
/* Prevents multiple tasks from printing at the same time */
Semaphore_Handle displayMutexHandle;
Semaphore_Struct displayMutexStruct;

/* ======== Shared Input Buffer ======== */
char consoleInput[CONSOLE_INPUT_MAX];

/* ======== Parsed Values ======== */
/* Stores the last successfully parsed integer from console */
int  consoleIntValue   = 0;
bool consoleHasInteger = false;

extern void* setPWM(int uS);
extern void* startPWM(void);

/*
 *  ======== Console_lock / Console_unlock ========
 *  Lock mutex before printing
 *  Unlock after printing
 *  Never call scanf inside a lock — causes deadlock
 */
void Console_lock(void) {
    Semaphore_pend(displayMutexHandle, BIOS_WAIT_FOREVER);
}

void Console_unlock(void) {
    Semaphore_post(displayMutexHandle);
}

/*
 *  ======== parseInteger ========
 *  Safely converts string input to integer using strtol
 *  Returns true if conversion succeeded, false if input was not a number
 */
bool parseInteger(char *input, int *outValue) {
    char *endPtr;

    /* strtol returns 0 and sets endPtr == input if no valid conversion */
    *outValue = (int)strtol(input, &endPtr, 10);

    /* If endPtr did not move, no digits were found */
    if(endPtr == input) {
        return false;
    }

    return true;
}

/*
 *  ======== consoleReadInput ========
 *  Reads a line of input from CCS Debug Console
 *  Posts semaphore when input is ready
 */
void consoleReadInput(void) {
    /* Clear shared buffer before reading */
    memset(consoleInput, 0, sizeof(consoleInput));

    /* Lock before printing prompt */
    Console_lock();
    printf("Enter command or integer: ");
    fflush(stdout);
    Console_unlock();

    /* Flush any leftover newline BEFORE reading */
    fflush(stdin);

    /* Space before % skips leading whitespace and leftover newlines */
    scanf(" %63[^\n]", consoleInput);

    /* Signal consoleTask that input is ready to process */
    Semaphore_post(consoleSemHandle);
}

/*
 *  ======== consoleHandleInput ========
 *  Processes the string input from consoleInput buffer
 *  Attempts integer conversion and handles string commands
 */
void consoleHandleInput(char *input) {
    int parsedValue = 0;

    /* Ignore empty input */
    if(strlen(input) == 0) {
        Console_lock();
        printf("No input received, try again\n");
        Console_unlock();
        return;
    }

    /* Print raw received string */
    Console_lock();
    printf("Received: %s\n", input);
    Console_unlock();

    /* ---- Attempt integer conversion first ---- */
    if(parseInteger(input, &parsedValue)) {
        /* Conversion succeeded — store and display integer */
        consoleIntValue   = parsedValue;
        consoleHasInteger = true;
        setPWM(1200);
        startPWM();

        Console_lock();
        printf("Integer value: %d\n", consoleIntValue);
        Console_unlock();
        return; // integer handled, skip command parsing
    }

    /* ---- Not an integer — handle as string command ---- */
    consoleHasInteger = false;

    if(strcmp(input, "post") == 0) {
        Console_lock();
        printf("Triggering HTTP POST...\n");
        Console_unlock();
        // call your httpsPost() here
    }
    else if(strcmp(input, "help") == 0) {
        Console_lock();
        printf("Available commands:\n");
        printf("  post     - send HTTP POST\n");
        printf("  help     - show this menu\n");
        printf("  quit     - stop console task\n");
        printf("  <number> - store integer value\n");
        Console_unlock();
    }
    else if(strcmp(input, "quit") == 0) {
        Console_lock();
        printf("Exiting console task\n");
        Console_unlock();
        Task_exit();
    }
    else {
        Console_lock();
        printf("Unknown command: '%s'\n", input);
        printf("Type 'help' for available commands\n");
        Console_unlock();
    }
}

/*
 *  ======== consoleTask ========
 *  Main console task loop
 *  1. Reads input and posts semaphore
 *  2. Pends on semaphore
 *  3. Processes input
 *  4. Sleeps before next iteration
 */
void consoleTask(uintptr_t arg0, uintptr_t arg1) {
    Bool semAcquired;

    printf("Console task started\n");
    printf("Type 'help' for available commands\n");

    while(1) {
        /* Step 1 — Read input from console, posts semaphore internally */
        consoleReadInput();

        /* Step 2 — Pend on semaphore, waits up to CONSOLE_SEM_TIMEOUT_MS */
        semAcquired = Semaphore_pend(consoleSemHandle, MS_TO_TICKS(CONSOLE_SEM_TIMEOUT_MS));

        /* Step 3 — Check if semaphore was acquired or timed out */
        if(!semAcquired) {
            printf("Semaphore timeout, retrying...\n");
            continue;   // skip processing, loop back to read again
        }

        /* Step 4 — Semaphore acquired, process the input */
        consoleHandleInput(consoleInput);

        /* Step 5 — Sleep before next read, yields CPU to other tasks */
        Task_sleep(MS_TO_TICKS(CONSOLE_SLEEP_MS));
    }
}

/*
 *  ======== createConsoleTask ========
 *  Initializes semaphores, mutex, and console task
 *  Call ONCE from mainThread() before BIOS_start()
 */
void createConsoleTask(void) {
    Task_Params      taskParams;
    Semaphore_Params semParams;

    /* Disable buffering FIRST before anything else prints */
    setvbuf(stdin,  NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    /* ---- Create console mutex (init to 1 = unlocked) ---- */
    Semaphore_Params_init(&semParams);
    semParams.mode = Semaphore_Mode_BINARY;
    Semaphore_construct(&displayMutexStruct, 1, &semParams);
    displayMutexHandle = Semaphore_handle(&displayMutexStruct);

    if(displayMutexHandle == NULL) {
        printf("Failed to create console mutex\n");
        return;
    }
    printf("Console mutex created\n");

    /* ---- Create console semaphore (init to 0 = locked) ---- */
    Semaphore_Params_init(&semParams);
    semParams.mode = Semaphore_Mode_BINARY;
    Semaphore_construct(&consoleSemStruct, 0, &semParams);
    consoleSemHandle = Semaphore_handle(&consoleSemStruct);

    if(consoleSemHandle == NULL) {
        printf("Failed to create console semaphore\n");
        return;
    }
    printf("Console semaphore created\n");

    /* ---- Create console task ---- */
    Task_Params_init(&taskParams);
    taskParams.stack     = consoleTaskStack;
    taskParams.stackSize = CONSOLE_TASK_STACK_SIZE;
    taskParams.priority  = CONSOLE_TASK_PRIORITY;
    taskParams.arg0      = 0;
    taskParams.arg1      = 0;

    Task_construct(&consoleTaskStruct, consoleTask, &taskParams, NULL);
    consoleTaskHandle = Task_handle(&consoleTaskStruct);

    if(consoleTaskHandle == NULL) {
        printf("Failed to create console task\n");
    } else {
        printf("Console task created successfully\n");
    }
}
