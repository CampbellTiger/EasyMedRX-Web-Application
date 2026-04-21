/*
 *          ======== add_func.c ========
 *  add useful/needed functions to schedule here
 */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

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
#include "error_handler.h"
#include "timeKeep.h"
#include "http_queue.h"
#include "platform.h"

/* RC522_senseRFID_task is now spawned by initLCD_hardware() in LCD_Menu.c */

/* Display handle */
extern Display_Handle display;

/* External objects */
extern sem_t sem;

/* ================================================================
 *  printFieldDump()
 *
 *  Print every key-value pair for a parsed JSON object handle.
 *  Scaled per template type -- add a new case here whenever a
 *  new Json_TemplateType_t is added to json_make.h.
 * ================================================================ */
static void printFieldDump(Json_Handle obj, Json_TemplateType_t type)
{
    Display_printf(display, 0, 0, "[ENTER] printFieldDump\n\r");
    char     strVal[64] = {0};
    int32_t  intVal     = 0;
    uint16_t sz;
    int16_t  ret;
    int      i;

    /* ---- UpdateMCU field tables (matches templateFile exactly) ---- */
    /* Strings: type(0), uid(1), user(2), pword(3), email(4),
     *          script0-3(5-8), device0-4(9-13)  →  14 total          */
    static const char *mcuStrKeys[] = {
        "\"type\"",    "\"uid\"",     "\"user\"",    "\"pword\"",
        "\"email\"",
        "\"script0\"", "\"script1\"", "\"script2\"", "\"script3\"",
        "\"device0\"", "\"device1\"", "\"device2\"", "\"device3\"", "\"device4\""
    };
    /* Ints: dose0-3, window0-3, time  →  9 total.
     * stock0-3 are NOT in templateFile; they live in compStockTemplate. */
    static const char *mcuIntKeys[] = {
        "\"dose0\"",   "\"dose1\"",   "\"dose2\"",   "\"dose3\"",
        "\"window0\"", "\"window1\"", "\"window2\"", "\"window3\"",
        "\"time\""
    };

    /* ---- Error field tables (matches errorTemplate exactly) ---- */
    /* Strings: type, uid, message  →  3 total                        */
    static const char *errStrKeys[] = {
        "\"type\"",  "\"uid\"",  "\"message\""
    };

    static const char *errIntKeys[] = { "\"time\"" };

    /* ---- compStock field tables (matches compStockTemplate exactly) ---- */
    /* Strings: medicine0-3  →  4 total                                */
    static const char *csStrKeys[] = {
        "\"medicine0\"", "\"medicine1\"",
        "\"medicine2\"", "\"medicine3\""
    };
    /* Ints: stock0-3  →  4 total.
     * dose0-3 are NOT in compStockTemplate; they live in templateFile. */
    static const char *csIntKeys[] = {
        "\"stock0\"", "\"stock1\"", "\"stock2\"", "\"stock3\""
    };

    switch (type)
    {
        /* ---- UpdateMCU ---- */
        case TEMPLATE_UPDATE_MCU:
            /* i=0..4: type, uid, user, pword, email */
            Display_printf(display, 0, 0, "  -- String fields --\n\r");
            for (i = 0; i < 5; i++) {
                sz = sizeof(strVal); memset(strVal, 0, sizeof(strVal));
                ret = Json_getValue(obj, mcuStrKeys[i], strVal, &sz);
                if (ret >= 0)
                    Display_printf(display, 0, 0,
                        "  %-12s : %s\n\r", mcuStrKeys[i], strVal);
            }
            /* i=5..8: script0-3 */
            Display_printf(display, 0, 0, "  -- Script fields --\n\r");
            for (i = 5; i < 9; i++) {
                sz = sizeof(strVal); memset(strVal, 0, sizeof(strVal));
                ret = Json_getValue(obj, mcuStrKeys[i], strVal, &sz);
                if (ret >= 0)
                    Display_printf(display, 0, 0,
                        "  %-12s : %s\n\r", mcuStrKeys[i], strVal);
            }
            /* i=9..13: device0-4 */
            Display_printf(display, 0, 0, "  -- Device fields --\n\r");
            for (i = 9; i < 14; i++) {
                sz = sizeof(strVal); memset(strVal, 0, sizeof(strVal));
                ret = Json_getValue(obj, mcuStrKeys[i], strVal, &sz);
                if (ret >= 0)
                    Display_printf(display, 0, 0,
                        "  %-12s : %s\n\r", mcuStrKeys[i], strVal);
            }
            /* dose0-3, window0-3, time */
            Display_printf(display, 0, 0, "  -- Int32 fields --\n\r");
            for (i = 0; i < 9; i++) {
                sz = sizeof(int32_t); intVal = 0;
                ret = Json_getValue(obj, mcuIntKeys[i], &intVal, &sz);
                if (ret >= 0)
                    Display_printf(display, 0, 0,
                        "  %-12s : %d\n\r", mcuIntKeys[i], intVal);
            }
            break;

        /* ---- Error ---- */
        case TEMPLATE_ERROR:
            /* type, uid, message  →  3 strings */
            Display_printf(display, 0, 0, "  -- String fields --\n\r");
            for (i = 0; i < 3; i++) {
                sz = sizeof(strVal); memset(strVal, 0, sizeof(strVal));
                ret = Json_getValue(obj, errStrKeys[i], strVal, &sz);
                if (ret >= 0)
                    Display_printf(display, 0, 0,
                        "  %-12s : %s\n\r", errStrKeys[i], strVal);
            }
            Display_printf(display, 0, 0, "  -- Int32 fields --\n\r");
            sz = sizeof(int32_t); intVal = 0;
            ret = Json_getValue(obj, errIntKeys[0], &intVal, &sz);
            if (ret >= 0)
                Display_printf(display, 0, 0,
                    "  %-12s : %d\n\r", errIntKeys[0], intVal);
            break;

        /* ---- compStock ---- */
        case TEMPLATE_COMP_STOCK:
            /* medicine0-3  →  4 strings */
            Display_printf(display, 0, 0, "  -- Medicine names --\n\r");
            for (i = 0; i < 4; i++) {
                sz = sizeof(strVal); memset(strVal, 0, sizeof(strVal));
                ret = Json_getValue(obj, csStrKeys[i], strVal, &sz);
                if (ret >= 0)
                    Display_printf(display, 0, 0,
                        "  %-14s : %s\n\r", csStrKeys[i], strVal);
            }
            /* stock0-3  →  4 ints (dose0-3 are in templateFile, not here) */
            Display_printf(display, 0, 0, "  -- Stock (compStock) --\n\r");
            for (i = 0; i < 4; i++) {
                sz = sizeof(int32_t); intVal = 0;
                ret = Json_getValue(obj, csIntKeys[i], &intVal, &sz);
                if (ret >= 0)
                    Display_printf(display, 0, 0,
                        "  %-14s : %d\n\r", csIntKeys[i], intVal);
            }
            break;

        default:
            break;
    }
}

/* ================================================================
 *  printFSFileTyped()
 *
 *  Core FS print function:
 *    1. Opens file, finds real JSON length (scan back for '}').
 *    2. Calls removeUnwantedChars() to strip TI library spacing.
 *    3. Calls prettyDisplay() for indented terminal output.
 *    4. If tmplType != TEMPLATE_UNKNOWN, parses with the matching
 *       template and calls printFieldDump() for key-value breakdown.
 *
 *  To add a new file type: add a TEMPLATE_* value in json_make.h,
 *  add a case in printFieldDump(), then pass the new type here.
 * ================================================================ */
static void printFSFileTyped(const char *filename, Json_TemplateType_t tmplType)
{
    Display_printf(display, 0, 0, "[ENTER] printFSFileTyped\n\r");
    int32_t        fileHandle;
    int32_t        bytesRead;
    SlFsFileInfo_t fileInfo;
    static char    readBuf[MAX_SIZE];
    static char    parseBuf[MAX_SIZE];
    int16_t        i;
    uint16_t       jsonLen = 0;
    int16_t        retVal;

    Display_printf(display, 0, 0,
        "\r\n--- FS file: %s ---\n\r", filename);

    if (sl_FsGetInfo((unsigned char *)filename, 0, &fileInfo) < 0)
    {
        Display_printf(display, 0, 0, "  [does not exist]\n\r");
        return;
    }

    if (fileInfo.Len == 0)
    {
        Display_printf(display, 0, 0, "  [empty -- allocated but no content]\n\r");
        return;
    }

    fileHandle = sl_FsOpen((unsigned char *)filename, SL_FS_READ, 0);
    if (fileHandle < 0)
    {
        Display_printf(display, 0, 0, "  [cannot open, ret=%d]\n\r", fileHandle);
        return;
    }

    memset(readBuf, 0, MAX_SIZE);
    bytesRead = sl_FsRead(fileHandle, 0, (unsigned char *)readBuf, MAX_SIZE - 1);
    sl_FsClose(fileHandle, NULL, NULL, 0);

    if (bytesRead < 0)
    {
        Display_printf(display, 0, 0, "  [read failed, ret=%d]\n\r", bytesRead);
        return;
    }

    /* Find real JSON end -- SLFS pads to allocated size */
    for (i = bytesRead - 1; i >= 0; i--)
    {
        if (readBuf[i] == '}') { jsonLen = (uint16_t)(i + 1); break; }
    }

    if (jsonLen == 0)
    {
        Display_printf(display, 0, 0, "  [no closing brace -- file may be corrupt]\n\r");
        return;
    }

    readBuf[jsonLen] = '\0';
    Display_printf(display, 0, 0,
        "  allocated=%d  actual=%d bytes\n\r",
        (int)fileInfo.Len, (int)jsonLen);

    /* Keep a clean copy for parsing before compaction modifies readBuf */
    memcpy(parseBuf, readBuf, jsonLen + 1);

    /* Compact TI library spacing then pretty-print */
    removeUnwantedChars(readBuf);
    prettyDisplay(readBuf);

    /* Field dump only if a specific template type was requested */
    if (tmplType == TEMPLATE_UNKNOWN)
        return;

    extern Json_Filename_t templateFile;
    extern Json_Filename_t errorTemplate;
    extern Json_Filename_t compStockTemplate;

    Json_Filename_t tmplBuff;
    switch (tmplType)
    {
        case TEMPLATE_UPDATE_MCU:  tmplBuff = templateFile;       break;
        case TEMPLATE_ERROR:       tmplBuff = errorTemplate;      break;
        case TEMPLATE_COMP_STOCK:  tmplBuff = compStockTemplate;  break;
        default:                   return;
    }

    Json_Handle tmplHandle = 0;
    Json_Handle objHandle  = 0;

    createTemplate(&tmplBuff, &tmplHandle);
    retVal = Json_createObject(&objHandle, tmplHandle, 512);
    if (retVal < 0)
    {
        Display_printf(display, 0, 0,
            "  [field dump: createObject failed %d]\n\r", retVal);
        Json_destroyTemplate(tmplHandle);
        return;
    }

    /* Compact parseBuf before handing to Json_parse */
    removeUnwantedChars(parseBuf);
    retVal = Json_parse(objHandle, parseBuf, (uint16_t)strlen(parseBuf));
    if (retVal == 0)
        printFieldDump(objHandle, tmplType);
    else
        Display_printf(display, 0, 0,
            "  [field dump: parse failed %d]\n\r", retVal);

    Json_destroyObject(objHandle);
    Json_destroyTemplate(tmplHandle);
}

/* printFSFile() -- pretty-print only, no field dump */
static void printFSFile(const char *filename)
{
    Display_printf(display, 0, 0, "[ENTER] printFSFile\n\r");
    printFSFileTyped(filename, TEMPLATE_UNKNOWN);
}

/* ================================================================
 *  TEST 1 -- dumpFS()
 *
 *  Print every account file (json1-json4), compStock, and all
 *  error logs with their matching field dumps.
 *
 *  Uncomment the testDumpFS thread block in schedFuncs() to run.
 * ================================================================ */
static void dumpFS(void)
{
    Display_printf(display, 0, 0, "[ENTER] dumpFS\n\r");
    char candidate[16];
    int  i;

    Display_printf(display, 0, 0,
        "\r\n========================================\n\r");
    Display_printf(display, 0, 0, "  FS DUMP\n\r");
    Display_printf(display, 0, 0,
        "========================================\n\r");

    /* Account files -- UpdateMCU template */
    Display_printf(display, 0, 0, "\r\n[ Account files ]\n\r");
    printFSFileTyped(JSON_FILENAME1, TEMPLATE_UPDATE_MCU);
    printFSFileTyped(JSON_FILENAME2, TEMPLATE_UPDATE_MCU);
    printFSFileTyped(JSON_FILENAME3, TEMPLATE_UPDATE_MCU);
    printFSFileTyped(JSON_FILENAME4, TEMPLATE_UPDATE_MCU);

    /* Compartment stock -- compStock template */
    Display_printf(display, 0, 0, "\r\n[ Compartment stock ]\n\r");
    printFSFileTyped(COMP_STOCK_FILENAME, TEMPLATE_COMP_STOCK);

    /* Error logs -- Error template */
    Display_printf(display, 0, 0, "\r\n[ Error logs ]\n\r");
    int found = 0;
    for (i = 0; i < MAX_ERROR_FILES; i++)
    {
        SlFsFileInfo_t info;
        snprintf(candidate, sizeof(candidate), "%s%d", ERROR_BASE_NAME, i);
        if (sl_FsGetInfo((unsigned char *)candidate, 0, &info) >= 0)
        {
            printFSFileTyped(candidate, TEMPLATE_ERROR);
            found++;
        }
    }
    if (found == 0)
        Display_printf(display, 0, 0, "  [no error logs found]\n\r");

    Display_printf(display, 0, 0,
        "\r\n========================================\n\r");
    Display_printf(display, 0, 0,
        "  FS DUMP COMPLETE -- %d error log(s)\n\r", found);
    Display_printf(display, 0, 0,
        "========================================\n\r");
}

void* testDumpFS(void *pvParameters)
{
    Display_printf(display, 0, 0, "[ENTER] testDumpFS\n\r");
    dumpFS();
    return NULL;
}

/* ================================================================
 *  clearErrorLogs()
 *
 *  Delete every error0 error99 file that exists on the SimpleLink FS.
 *  Reports how many were deleted and how many slots were already empty.
 *
 *  Call this:
 *    - Before a fresh test run to avoid carryover from previous runs
 *    - As a maintenance function when the buffer approaches 100 slots
 *    - From the web application via a command flag if needed later
 *
 *  Does NOT touch json1-json4 or compStock.
 * ================================================================ */
void clearErrorLogs(void)
{
    Display_printf(display, 0, 0, "[ENTER] clearErrorLogs\n\r");
    char     candidate[16];
    int32_t  status;
    int      deleted = 0;
    int      skipped = 0;
    int      i;

    Display_printf(display, 0, 0,
        "\r\n========================================\n\r");
    Display_printf(display, 0, 0, "  CLEARING ERROR LOG BUFFER\n\r");
    Display_printf(display, 0, 0,
        "========================================\n\r");

    for (i = 0; i < MAX_ERROR_FILES; i++)
    {
        snprintf(candidate, sizeof(candidate), "%s%d", ERROR_BASE_NAME, i);

        SlFsFileInfo_t info;
        if (sl_FsGetInfo((unsigned char *)candidate, 0, &info) < 0)
        {
            skipped++;
            continue;   /* file does not exist nothing to delete */
        }

        status = sl_FsDel((unsigned char *)candidate, 0);
        if (status < 0)
            Display_printf(display, 0, 0,
                "  clearErrorLogs: failed to delete %s (ret=%d)\n\r",
                candidate, status);
        else
            deleted++;
    }

    Display_printf(display, 0, 0,
        "  Deleted: %d   Already empty: %d\n\r", deleted, skipped);
    Display_printf(display, 0, 0,
        "========================================\n\r");
}

void* testClearErrorLogs(void *pvParameters)
{
    Display_printf(display, 0, 0, "[ENTER] testClearErrorLogs\n\r");
    clearErrorLogs();
    return NULL;
}

/* ================================================================
 *  TEST 2 -- testErrorMessages()
 * ================================================================ */
void* testErrorMessages(void *pvParameters)
{
    Display_printf(display, 0, 0, "[ENTER] testErrorMessages\n\r");

    Display_printf(display, 0, 0,
        "\r\n========================================\n\r");
    Display_printf(display, 0, 0, "  ERROR MESSAGE TESTS\n\r");
    Display_printf(display, 0, 0,
        "========================================\n\r");

    const char *testFile = (activeFile != NULL) ? activeFile : JSON_FILENAME1;
    Display_printf(display, 0, 0, "Using test file: %s\n\r", testFile);

    Display_printf(display, 0, 0, "\r\n[TEST] DispensingError (-5)\n\r");
    logError(DispensingError, testFile, -1);
    sleep(1);

    Display_printf(display, 0, 0, "\r\n[TEST] OutOfPills (-4)\n\r");
    logError(OutOfPills, testFile, -1);
    sleep(1);

    sleep(1);

    Display_printf(display, 0, 0, "\r\n[TEST] FatalParsing (-1)\n\r");
    logError(FatalParsing, testFile, -1);
    sleep(1);

    Display_printf(display, 0, 0,
        "\r\n[TEST] Disconnected (-3) -- LCD only, no POST\n\r");
    logError(Disconnected, testFile, -1);
    sleep(1);

    Display_printf(display, 0, 0,
        "\r\n[TEST] OutOfPills with NULL activeFile -- POST should be skipped\n\r");
    logError(OutOfPills, NULL, -1);
    sleep(1);

    Display_printf(display, 0, 0,
        "\r\n[TEST] Dumping FS to verify error files were written...\n\r");
    dumpFS();

    Display_printf(display, 0, 0,
        "\r\n========================================\n\r");
    Display_printf(display, 0, 0, "  ERROR TESTS COMPLETE\n\r");
    Display_printf(display, 0, 0,
        "========================================\n\r");

    return NULL;
}

/* ================================================================
 *  TEST 3 -- testStockDecrement()
 * ================================================================ */
void* testStockDecrement(void *pvParameters)
{
    Display_printf(display, 0, 0, "[ENTER] testStockDecrement\n\r");

    Display_printf(display, 0, 0,
        "\r\n========================================\n\r");
    Display_printf(display, 0, 0, "  STOCK DECREMENT TEST\n\r");
    Display_printf(display, 0, 0,
        "========================================\n\r");

    const char *testFile = (activeFile != NULL) ? activeFile : JSON_FILENAME1;
    int i;

    Display_printf(display, 0, 0, "\r\nInitial stock:\n\r");
    for (i = 0; i < 4; i++)
    {
        int32_t stock = getCompartStock(i);
        int32_t dose  = getCompartDose(i);
        Display_printf(display, 0, 0,
            "  Compartment %d:  stock=%d  dose=%d\n\r",
            i, (int)stock, (int)dose);
    }

    Display_printf(display, 0, 0,
        "\r\nDecrementing compartment 0 three times...\n\r");
    for (i = 0; i < 3; i++)
    {
        int ret = decrementCompartStock(0, testFile);
        int32_t stock = getCompartStock(0);
        Display_printf(display, 0, 0,
            "  Decrement %d:  ret=%d  stock now=%d\n\r",
            i + 1, ret, (int)stock);
        sleep(1);
    }

    Display_printf(display, 0, 0,
        "\r\nVerifying compStock on FS after decrements:\n\r");
    printFSFileTyped(COMP_STOCK_FILENAME, TEMPLATE_COMP_STOCK);

    Display_printf(display, 0, 0,
        "\r\nForcing compartment 3 to zero to trigger OutOfPills...\n\r");
    {
        int32_t  one = 1;
        uint16_t sz  = sizeof(int32_t);
        int16_t  ret = Json_setValue(csObjHandle, "\"stock3\"", &one, sz);
        Display_printf(display, 0, 0, "  Set stock3=1: ret=%d\n\r", ret);

        ret = decrementCompartStock(3, testFile);
        Display_printf(display, 0, 0,
            "  decrementCompartStock(3) ret=%d  stock now=%d\n\r",
            ret, (int)getCompartStock(3));
        Display_printf(display, 0, 0,
            "  (OutOfPills logError should have fired above)\n\r");
    }

    Display_printf(display, 0, 0, "\r\nFinal FS state:\n\r");
    dumpFS();

    Display_printf(display, 0, 0,
        "\r\n========================================\n\r");
    Display_printf(display, 0, 0, "  STOCK TEST COMPLETE\n\r");
    Display_printf(display, 0, 0,
        "========================================\n\r");

    return NULL;
}

/* ================================================================
 *  Function creation
 * ================================================================ */
void* printConsole(void* arg0)
{
    Display_printf(display, 0, 0, "[ENTER] printConsole\n\r");
    char* mes = (char*)arg0;
    while(1)
    {
        printf("%s\n", mes);
        sleep(3);
    }
    return NULL;
}

/* ================================================================
 *  errorFlushThread()
 *
 *  Persistent low-priority background thread that continuously cycles
 *  through the error buffer (error0..error99).  On each pass it calls
 *  flushErrorBuffer() to POST any pending error files to the server
 *  and delete those acknowledged with return_code == 0.
 *
 *  Sleeps 60 seconds between passes so it does not saturate the
 *  network or NWP.  Runs at priority 1 (lowest) so it never
 *  interferes with RFID scanning, LCD drawing, or HTTP POSTs
 *  triggered by normal operation.
 *
 *  Spawned unconditionally by schedFuncs() -- always active.
 * ================================================================ */
void* errorFlushThread(void *pvParameters)
{
    Display_printf(display, 0, 0, "[ENTER] errorFlushThread\n\r");
    /* Gate on sem to ensure schedFuncs and initLCD_hardware have both
     * completed before the first NWP access from this thread.
     * Acquire and immediately release -- we only need the ordering
     * guarantee, not mutual exclusion for the flush itself.          */
    sem_wait(&sem);
    sem_post(&sem);

    /* Wait for SNTP sync before first flush (up to 30s).
     * If SNTP fails, proceed anyway -- timestamps will be 0
     * but error files still get posted.                      */
    {
        int waitSecs = 0;
        while (!TimeKeeper_isValid() && waitSecs < 30)
        {
            sleep(1);
            waitSecs++;
        }
    }

    while (1)
    {
        flushErrorBuffer();

        /* 15-second cooldown after each pass completes */
        sleep(15);
    }
    return NULL;
}

/* ================================================================
 *  syncPollingThread
 *
 *  Every 15 seconds: POSTs compStock and trustedUIDs files to the
 *  server and displays each response body.  Uses httpQ_postGetResponse()
 *  so the HTTP worker executes each call and unblocks this thread once
 *  the response arrives.
 *
 *  Gates on sem to ensure jsonThread (csObjHandle) and the HTTP
 *  queue are initialised before the first POST.
 *  Spawned from schedFuncs() at priority 2.
 * ================================================================ */
void* syncPollingThread(void *pvParameters)
{
    Display_printf(display, 0, 0, "[ENTER] syncPollingThread\n\r");

    sem_wait(&sem);
    sem_post(&sem);

    static char stockRespBuf[JSON_SIZE];
    static char uidRespBuf[JSON_SIZE];

    while (1)
    {
        /* ---- compStock ---- */
        Display_printf(display, 0, 0,
            "syncPollingThread: posting %s\n\r", COMP_STOCK_FILENAME);

        memset(stockRespBuf, 0, sizeof(stockRespBuf));
        int ret = httpQ_postGetResponse(COMP_STOCK_FILENAME,
                                        stockRespBuf,
                                        (uint16_t)sizeof(stockRespBuf));
        if (ret == 0 && stockRespBuf[0] != '\0')
        {
            Display_printf(display, 0, 0,
                "syncPollingThread: compStock response = %s\n\r", stockRespBuf);
        }
        else
        {
            Display_printf(display, 0, 0,
                "syncPollingThread: compStock no response (ret=%d)\n\r", ret);
            if (g_lastHttpConnectRet == -111)
            {
                Display_printf(display, 0, 0,
                    "syncPollingThread: server refused compStock -- disconnecting and reconnecting\n\r");
                checkAndReconnect();
                Display_printf(display, 0, 0,
                    "syncPollingThread: reconnect issued, waiting 15s before retrying\n\r");
                sleep(15);
                continue;
            }
        }

        /* ---- trustedUIDs ---- */
        Display_printf(display, 0, 0,
            "syncPollingThread: posting %s\n\r", TRUSTED_UID_FILENAME);

        memset(uidRespBuf, 0, sizeof(uidRespBuf));
        ret = httpQ_postGetResponse(TRUSTED_UID_FILENAME,
                                    uidRespBuf,
                                    (uint16_t)sizeof(uidRespBuf));
        if (ret == 0 && uidRespBuf[0] != '\0')
        {
            Display_printf(display, 0, 0,
                "syncPollingThread: trustedUIDs response = %s\n\r", uidRespBuf);
        }
        else
        {
            Display_printf(display, 0, 0,
                "syncPollingThread: trustedUIDs no response (ret=%d)\n\r", ret);
            if (g_lastHttpConnectRet == -111)
            {
                Display_printf(display, 0, 0,
                    "syncPollingThread: server refused trustedUIDs -- disconnecting and reconnecting\n\r");
                checkAndReconnect();
                Display_printf(display, 0, 0,
                    "syncPollingThread: reconnect issued, waiting 15s before retrying\n\r");
                sleep(15);
                continue;
            }
        }

        /* ---- onlineLogin ---- */
        ret = onlineLogin();
        if (ret == -1 && g_lastHttpConnectRet == -111)
        {
            Display_printf(display, 0, 0,
                "syncPollingThread: server refused onlineLogin -- disconnecting and reconnecting\n\r");
            checkAndReconnect();
            Display_printf(display, 0, 0,
                "syncPollingThread: reconnect issued, waiting 15s before retrying\n\r");
            sleep(15);
            continue;
        }

        sleep(15);
    }
    return NULL;
}

/* ================================================================
 *  Function scheduling
 *  Uncomment one test block at a time. Always leave jsonThread
 *  enabled -- csObjHandle and account files must be ready before
 *  any test thread runs.
 * ================================================================ */
void* schedFuncs(void* pvParameters)
{
    Display_printf(display, 0, 0, "[ENTER] schedFuncs\n\r");
    sem_wait(&sem);

    /* Initialise the HTTP queue mutex and semaphore before any code that
     * may call httpQ_post() runs (jsonThread -> storeJsonToFile -> httpQ_post).
     * Must precede the inline jsonThread() call below.                     */
    httpQ_init();

    /* Run jsonThread body inline so all JSON init completes while schedFuncs
     * already holds sem. This prevents httpPost being called from a spawned
     * thread that races to acquire sem, which caused Error_SPIN at boot. */
    Display_printf(display, 0, 0, "\rjsonThread() running inline\n\r");
    jsonThread(NULL);
    Display_printf(display, 0, 0, "\rjsonThread() complete\n\r");

    /* Start SNTP now -- jsonThread has finished all NWP FS operations
     * so the NWP is idle. SlNetSock is already initialised by the
     * IP-acquired handler, so SNTP_getTime will succeed.          */
    TimeKeeper_start();


    /* RC522_senseRFID is spawned by initLCD_hardware() in LCD_Menu.c
     * after the idle screen is drawn -- no need to schedule it here.    */

    /*Test SPI*/
    /*pthread_t SPIThread;
    pthread_attr_t SPIAttrs;
    struct sched_param SPISched;
    pthread_attr_init(&SPIAttrs);
    SPISched.sched_priority = 2;
    pthread_attr_setdetachstate(&SPIAttrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&SPIAttrs, &SPISched);
    pthread_attr_setstacksize(&SPIAttrs, 4096);
    pthread_create(&SPIThread, &SPIAttrs, testSPI, NULL);
    Display_printf(display, 0, 0,"\rtestSPI() scheduled: prio 2\n\r");*/

    /* initLCD_hardware -- one-time LCD init, draws "Scan RFID card" idle screen.
     * Must run before RC522_senseRFID starts polling.               */
    pthread_t HardwareThread;
    pthread_attr_t HardwareAttrs;
    struct sched_param HardwareSched;
    pthread_attr_init(&HardwareAttrs);
    HardwareSched.sched_priority = 9;
    pthread_attr_setdetachstate(&HardwareAttrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&HardwareAttrs, &HardwareSched);
    pthread_attr_setstacksize(&HardwareAttrs, 4096);
    pthread_create(&HardwareThread, &HardwareAttrs, initLCD_hardware, NULL);
    Display_printf(display, 0, 0, "\rinitHardware_hardware() scheduled: prio 9\n\r");

    sem_post(&sem);   /* release boot-ordering sem -- initLCD_hardware and errorFlushThread can now proceed */

    /* httpWorkerThread -- sole owner of NWP HTTP access, serialises all POSTs */
    pthread_t          httpWrkThread;
    pthread_attr_t     httpWrkAttrs;
    struct sched_param httpWrkSched;
    pthread_attr_init(&httpWrkAttrs);
    httpWrkSched.sched_priority = 4;
    pthread_attr_setdetachstate(&httpWrkAttrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&httpWrkAttrs, &httpWrkSched);
    pthread_attr_setstacksize(&httpWrkAttrs, 4096);
    pthread_create(&httpWrkThread, &httpWrkAttrs, httpWorkerThread, NULL);
    Display_printf(display, 0, 0, "\rhttpWorkerThread() scheduled: prio 4\n\r");

    /*clearErrorLogs -- deletes all error0-error99 files from the FS*/
    pthread_t clearErrThread;
    pthread_attr_t clearErrAttrs;
    struct sched_param clearErrSched;
    pthread_attr_init(&clearErrAttrs);
    clearErrSched.sched_priority = 5;
    pthread_attr_setdetachstate(&clearErrAttrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&clearErrAttrs, &clearErrSched);
    pthread_attr_setstacksize(&clearErrAttrs, 1024);
    pthread_create(&clearErrThread, &clearErrAttrs, testClearErrorLogs, NULL);
    Display_printf(display, 0, 0,"\rtestClearErrorLogs() scheduled: prio 5\n\r");

    /*testDumpFS -- prints all account files, compStock, and error logs with field dumps*/
    /*pthread_t dumpThread;
    pthread_attr_t dumpAttrs;
    struct sched_param dumpSched;
    pthread_attr_init(&dumpAttrs);
    dumpSched.sched_priority = 5;
    pthread_attr_setdetachstate(&dumpAttrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&dumpAttrs, &dumpSched);
    pthread_attr_setstacksize(&dumpAttrs, 4096);
    pthread_create(&dumpThread, &dumpAttrs, testDumpFS, NULL);
    Display_printf(display, 0, 0,"\rtestDumpFS() scheduled: prio 5\n\r");*/

    /*testErrorMessages -- fires each error code and verifies POST + FS write*/
    /*pthread_t errThread;
    pthread_attr_t errAttrs;
    struct sched_param errSched;
    pthread_attr_init(&errAttrs);
    errSched.sched_priority = 5;
    pthread_attr_setdetachstate(&errAttrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&errAttrs, &errSched);
    pthread_attr_setstacksize(&errAttrs, 4096);
    pthread_create(&errThread, &errAttrs, testErrorMessages, NULL);
    Display_printf(display, 0, 0,"\rtestErrorMessages() scheduled: prio 5\n\r");*/

    /*testStockDecrement -- verifies decrement, FS flush, and OutOfPills trigger*/
    /*pthread_t stockThread;
    pthread_attr_t stockAttrs;
    struct sched_param stockSched;
    pthread_attr_init(&stockAttrs);
    stockSched.sched_priority = 5;
    pthread_attr_setdetachstate(&stockAttrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&stockAttrs, &stockSched);
    pthread_attr_setstacksize(&stockAttrs, 4096);
    pthread_create(&stockThread, &stockAttrs, testStockDecrement, NULL);
    Display_printf(display, 0, 0,"\rtestStockDecrement() scheduled: prio 5\n\r");*/

    /*TestDemux*/
    /*pthread_t demuxThread;
    pthread_attr_t demuxAttrs;
    struct sched_param demuxSched;
    pthread_attr_init(&demuxAttrs);
    demuxSched.sched_priority = 9;
    pthread_attr_setdetachstate(&demuxAttrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&demuxAttrs, &demuxSched);
    pthread_attr_setstacksize(&demuxAttrs, 4096);
    pthread_create(&demuxThread, &demuxAttrs, testDemux, NULL);
    Display_printf(display, 0, 0,"\rtestDemux() scheduled: prio 9\n\r");*/

    /*Test PWM*/
    /*pthread_t testPWMThread;
    pthread_attr_t testPWMAttrs;
    struct sched_param testPWMSched;
    pthread_attr_init(&testPWMAttrs);
    testPWMSched.sched_priority = 8;
    pthread_attr_setdetachstate(&testPWMAttrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&testPWMAttrs, &testPWMSched);
    pthread_attr_setstacksize(&testPWMAttrs, 4096);
    pthread_create(&testPWMThread, &testPWMAttrs, testPWM, NULL);
    Display_printf(display, 0, 0,"\rtestPWM() scheduled: prio 8\n\r");*/

    /* errorFlushThread -- background error buffer drain, always active.
     * Stack must be large enough for flushErrorBuffer's respBuf[JSON_SIZE]
     * (2048 B) plus httpQ_postGetResponse's frame and errorFlushThread's
     * own frame.  4096 was too small and triggered Error_SPIN on boot. */
    pthread_t          flushThread;
    pthread_attr_t     flushAttrs;
    struct sched_param flushSched;
    pthread_attr_init(&flushAttrs);
    flushSched.sched_priority = 1;
    pthread_attr_setdetachstate(&flushAttrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&flushAttrs, &flushSched);
    pthread_attr_setstacksize(&flushAttrs, 8192);
    pthread_create(&flushThread, &flushAttrs, errorFlushThread, NULL);
    Display_printf(display, 0, 0, "\rerrorFlushThread() scheduled: prio 1\n\r");

    /* syncPollingThread -- POSTs compStock and trustedUIDs every 15 s.
     * Stack: 2048.  Both response buffers are declared static (BSS), so
     * they do NOT contribute to the stack frame.  Peak stack is
     * httpQ_postGetResponse (~100 B) + syncPollingThread frame (~50 B)
     * = well under 2048.                                                     */
    pthread_t          stockThread;
    pthread_attr_t     stockAttrs;
    struct sched_param stockSched;
    pthread_attr_init(&stockAttrs);
    stockSched.sched_priority = 2;
    pthread_attr_setdetachstate(&stockAttrs, PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedparam(&stockAttrs, &stockSched);
    pthread_attr_setstacksize(&stockAttrs, 2048);
    pthread_create(&stockThread, &stockAttrs, syncPollingThread, NULL);
    Display_printf(display, 0, 0, "\rsyncPollingThread() scheduled: prio 2\n\r");

    return NULL;
}
