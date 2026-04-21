#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <semaphore.h>
#include "json_make.h"
#include "error_handler.h"
#include "http_queue.h"

/* http_comm.c -- called directly (not via queue) during jsonThread init
 * because httpWorkerThread is not yet spawned at that point.            */
extern int httpPostGetResponse(const char *filename,
                               char *respBuf, uint16_t respBufLen);


/* dispPills incremented per successful dispense and reported to web app via updateWebApp */
/*dose - dips Pills = remaining pills dispenable per session*/
/*store edited in active profile session to not change original profile aka clear when exit*/
/* dispPills0-3 removed from template: they are never read/written via
 * the Json API.  Dispensed pill counts are tracked in g_dispPills[] RAM
 * and sent to the server via snprintf-built updateWebApp JSON, not via
 * the profile file.  Removing them shrinks every FS profile file by ~60B. */
Json_Filename_t templateFile = {
        .fileBuffer =
        "{"
        "\"type\":string,"
        "\"uid\":string,"
        "\"user\":string,"
        "\"pword\":string,"
        "\"email\":string,"
        "\"script0\":string,"
        "\"script1\":string,"
        "\"script2\":string,"
        "\"script3\":string,"
        "\"device0\":string,"
        "\"device1\":string,"
        "\"device2\":string,"
        "\"device3\":string,"
        "\"device4\":string,"
        "\"dose0\":int32,"
        "\"dose1\":int32,"
        "\"dose2\":int32,"
        "\"dose3\":int32,"
        "\"window0\":int32,"
        "\"window1\":int32,"
        "\"window2\":int32,"
        "\"window3\":int32,"
        "\"time\":int32"
        "}",

        .fileSize = sizeof(
        "{"
        "\"type\":string,"
        "\"uid\":string,"
        "\"user\":string,"
        "\"pword\":string,"
        "\"email\":string,"
        "\"script0\":string,"
        "\"script1\":string,"
        "\"script2\":string,"
        "\"script3\":string,"
        "\"device0\":string,"
        "\"device1\":string,"
        "\"device2\":string,"
        "\"device3\":string,"
        "\"device4\":string,"
        "\"dose0\":int32,"
        "\"dose1\":int32,"
        "\"dose2\":int32,"
        "\"dose3\":int32,"
        "\"window0\":int32,"
        "\"window1\":int32,"
        "\"window2\":int32,"
        "\"window3\":int32,"
        "\"time\":int32"
        "}")-1,

        .templateType = TEMPLATE_UPDATE_MCU
};
/* Default firmware profile — matches templateFile exactly.
 * window0-3 are zeroed (server assigns live values on first sync).
 * dispPills0-3 removed — tracked in RAM only, not stored in profile. */
const char* jsonString =
        "{"
        "\"type\":\"updateMCUProfile\","
        "\"uid\":\"87447d33\","
        "\"user\":\"Bugs\","
        "\"pword\":\"Bunny\","
        "\"email\":\"DaphyDuck@gmail.com\","
        "\"script0\":\"Tic-Tacs\","
        "\"script1\":\"Gobstoppers\","
        "\"script2\":\"Atomic Fireballs\","
        "\"script3\":\"Skittles\","
        "\"device0\":\"\","
        "\"device1\":\"\","
        "\"device2\":\"\","
        "\"device3\":\"\","
        "\"device4\":\"\","
        "\"dose0\":10,"
        "\"dose1\":10,"
        "\"dose2\":100,"
        "\"dose3\":70,"
        "\"window0\":0,"
        "\"window1\":0,"
        "\"window2\":0,"
        "\"window3\":0,"
        "\"time\":0"
        "}";

Json_Filename_t errorTemplate = {
        .fileBuffer =
        "{"
        "\"type\":string,"
        "\"uid\":string,"
        "\"message\":string,"
        "\"time\":int32"
        "}",

        .fileSize = sizeof(
        "{"
        "\"type\":string,"
        "\"uid\":string,"
        "\"message\":string,"
        "\"time\":int32"
        "}")-1,

        .templateType = TEMPLATE_ERROR
};

/* Compartment Stock Template
 *
 *  Global machine file not tied to any user.
 *  4 compartments, each with:
 *    medicine# : string  medication name
 *    dose#     : int32   pills dispensed per scheduled dose
 *    stock#    : int32   pills currently remaining
 *
 *  This is the authoritative runtime stock counter.
 *  Decrement stock# here on every successful dispense.
 *  The stock values in json1-4 represent the initial fill
 *  at registration only do not read them for dispense logic.
 */
Json_Filename_t compStockTemplate = {
        .fileBuffer =
        "{"
        "\"type\":string,"
        "\"medicine0\":string,"
        "\"medicine1\":string,"
        "\"medicine2\":string,"
        "\"medicine3\":string,"
        "\"stock0\":int32,"
        "\"stock1\":int32,"
        "\"stock2\":int32,"
        "\"stock3\":int32"
        "}",

        .fileSize = sizeof(
        "{"
        "\"type\":string,"
        "\"medicine0\":string,"
        "\"medicine1\":string,"
        "\"medicine2\":string,"
        "\"medicine3\":string,"
        "\"stock0\":int32,"
        "\"stock1\":int32,"
        "\"stock2\":int32,"
        "\"stock3\":int32"
        "}")-1,

        .templateType = TEMPLATE_COMP_STOCK
};

/* Trusted UID whitelist — uid0..uid7 map to up to 8 authorised cards.
 * Only UIDs present in this file can open a session on the device.   */
Json_Filename_t trustedUIDTemplate = {
        .fileBuffer =
        "{"
        "\"type\":string,"
        "\"uid0\":string,"
        "\"uid1\":string,"
        "\"uid2\":string,"
        "\"uid3\":string,"
        "\"uid4\":string,"
        "\"uid5\":string,"
        "\"uid6\":string,"
        "\"uid7\":string"
        "}",

        .fileSize = sizeof(
        "{"
        "\"type\":string,"
        "\"uid0\":string,"
        "\"uid1\":string,"
        "\"uid2\":string,"
        "\"uid3\":string,"
        "\"uid4\":string,"
        "\"uid5\":string,"
        "\"uid6\":string,"
        "\"uid7\":string"
        "}")-1,

        .templateType = TEMPLATE_TRUSTED_UID
};

const char *trustedUIDJsonString =
        "{"
        "\"type\":\"trustedUIDs\","
        "\"uid0\":\"87447d33\","
        "\"uid1\":\"\","
        "\"uid2\":\"\","
        "\"uid3\":\"\","
        "\"uid4\":\"\","
        "\"uid5\":\"\","
        "\"uid6\":\"\","
        "\"uid7\":\"\""
        "}";

/* Replace medicine names and counts to match the physical refill */
const char *compStockJsonString =
        "{"
        "\"type\":\"updateWebAppStock\","
        "\"medicine0\":\"Tic-Tacs\","
        "\"medicine1\":\"Gobstoppers\","
        "\"medicine2\":\"Atomic Fireballs\","
        "\"medicine3\":\"Skittles\","
        "\"stock0\":5000,"
        "\"stock1\":1000,"
        "\"stock2\":3000,"
        "\"stock3\":170"
        "}";


const char *onlineLoginJsonString =
        "{"
        "\"type\":\"onlineLogin\","
        "\"uid\":\"\""
        "}";

const char *jsonFiles[NUM_JSON_FILES] = { JSON_FILENAME1, JSON_FILENAME2, JSON_FILENAME3, JSON_FILENAME4 };
static char setFSFileSize[MAX_SIZE];
static char setTempBuffSize[MAX_SIZE];
static char setBuildSize[MAX_SIZE];

/* Handles  */
Json_Handle jsonHandle   = 0;   /* =0 means not yet created            */
Json_Handle templateHandle;
Json_Handle jsonObjHandle;

/* compStock handles live for the entire program lifetime.
 * Initialised in jsonThread(); never destroyed.                       */
Json_Handle csObjHandle  = 0;
Json_Handle csTmplHandle = 0;

/* trustedUIDs handles -- used only during jsonThread() init to parse
 * uid0-uid7 into g_trustedUIDs[].  Destroyed immediately after.     */
Json_Handle trustedObjHandle  = 0;
Json_Handle trustedTmplHandle = 0;

Json_Filename_t templateBuff;
Json_Filename_t jsonBuffer;
Json_Filename_t grabBuffer;

/* Active user file  */
/* Set by RC522_senseRFID() after a successful UID lookup.
 * Points into the jsonFiles[] array so no heap allocation needed.    */
const char *activeFile = NULL;

/* sessionActive
 *   1 = a user is logged in, LCD menu is running
 *   0 = no session, RFID scanner should be polling
 * Set to 1 by RC522_senseRFID() on successful UID match.
 * Set to 0 by cursorSelect() when Exit is chosen on MENU_MAIN.
 */
volatile uint8_t sessionActive = 0;

/* Per-session state -- valid only while a user is logged in.
 * Cleared by openActiveProfile() on login and closeActiveProfile() on logout.
 *
 * g_sessionWindow[]: window0-3 values received from the server with the profile.
 *   Kept in RAM only; stored as 0 in the FS file so the server always
 *   re-assigns them on the next sync.
 *
 * g_dispPills[]: cumulative pills dispensed per compartment this session.
 *   Reported to the web app via updateWebApp after each successful dispense.
 */
static int32_t g_sessionWindow[NUM_SCRIPT_SLOTS];
static int32_t g_dispPills[NUM_SCRIPT_SLOTS];

/* 0 during jsonThread init, 1 after sem_post -- gates httpPost in storeJsonToFile */
volatile uint8_t systemReady = 0;

/* HTTP POST/update provided by http_comm.c -- declared in json_make.h */

/* Global template buffer */
static char buff[JSON_SIZE];

/* Shared response buffer for synchronous HTTP round-trips.
 * syncProfileFromServer() runs pre-session (sessionActive == 0).
 * requestServerStock()    runs mid-session (sessionActive == 1).
 * The two are mutually exclusive by design, so sharing is safe.    */
static char g_httpRespBuf[JSON_SIZE];

/* Display handle */
extern Display_Handle display;

/* Semaphore */
extern sem_t sem;

/* Global variables */
int16_t templateSize;
uint16_t objSize = 1024;

/* Key tables used by getCompartStock / decrementCompartStock */
static const char *csStockKeys[] = {
    "\"stock0\"", "\"stock1\"", "\"stock2\"", "\"stock3\""
};
static const char *csDoseKeys[] = {
    "\"dose0\"", "\"dose1\"", "\"dose2\"", "\"dose3\""
};
static const char *csMedicineKeys[] = {
    "\"medicine0\"", "\"medicine1\"", "\"medicine2\"", "\"medicine3\""
};
static const char *csScriptKeys[] = {
    "\"script0\"", "\"script1\"", "\"script2\"", "\"script3\""
};
static const char *csDeviceKeys[] = {
    "\"device0\"", "\"device1\"", "\"device2\"", "\"device3\"", "\"device4\""
};
static const char *trustedUIDKeys[] = {
    "\"uid0\"", "\"uid1\"", "\"uid2\"", "\"uid3\"",
    "\"uid4\"", "\"uid5\"", "\"uid6\"", "\"uid7\""
};

/* Trusted UID cache -- copied from the JSON file at jsonThread() init.
 * The JSON handles (trustedObjHandle/trustedTmplHandle) are destroyed
 * immediately after to keep the simultaneous handle count within the
 * SimpleLink JSON library limit (≤4 at any time).                   */
static char          g_trustedUIDs[TRUSTED_UID_MAX_COUNT][32];
static volatile int  g_trustedUIDCount = 0;

static size_t my_strnlen(const char *s, size_t maxlen)
{
    size_t i;
    for (i = 0; i < maxlen && s[i] != '\0'; i++) {}
    return i;
}

int16_t readFile(Json_Filename_t *pBufferFile, char *FileName)
{
    Display_printf(display, 0, 0, "[ENTER] readFile\n\r");
    int32_t fileHandle = 0;
    int32_t Status = 0;
    uint32_t FileSize;
    int16_t retVal = 0;

    SlFsFileInfo_t FsFileInfo;

    Status = sl_FsGetInfo((unsigned char *)FileName, 0, &FsFileInfo);
    if(Status < 0)
    {
        Display_printf(display, 0, 0, "FS - Couldn't get info on file."
                                      " error status %d \n\r", Status);
        return(Status);
    }

    FileSize = FsFileInfo.Len;
    fileHandle = sl_FsOpen((unsigned char *)FileName, SL_FS_READ, 0);

    if(fileHandle < 0)
    {
        Display_printf(display, 0, 0, "FS - Couldn't open file. error status %d \n\r", fileHandle);
        return(fileHandle);
    }
    else
    {
        pBufferFile->fileBuffer = setFSFileSize;
        memset(pBufferFile->fileBuffer, '\0', FileSize + 1);

        Status = sl_FsRead(fileHandle, 0, (unsigned char *)pBufferFile->fileBuffer, FileSize);
        if(Status < 0)
        {
            Display_printf(display, 0, 0, "FS - Couldn't read file. error status %d \n\r", Status);
            sl_FsClose(fileHandle, NULL, NULL, 0);
            return(Status);
        }

        retVal = sl_FsClose(fileHandle, NULL, NULL, 0);
        if(retVal < 0)
        {
            Display_printf(display, 0, 0, "FS - Couldn't close file. error status %d \n\r", retVal);
            return(retVal);
        }

        Display_printf(display, 0, 0, "Read %s succesfully\n\r", FileName);
        return(Status);
    }
}

void removeUnwantedChars(char *pBuf)
{
    char *str_tmp;
    uint16_t i = 0, j = 0;
    str_tmp = pBuf;

    for(i = 0; str_tmp[i] != '\0'; ++i)
    {
        while((!(str_tmp[i] != '\n') ||
               !(str_tmp[i] != ' ')) && (str_tmp[i] != '\0'))
        {
            for(j = i; str_tmp[j] != '\0'; ++j)
                str_tmp[j] = str_tmp[j + 1];
            str_tmp[j] = '\0';
        }
    }
}

void prettyDisplay(char *json)
{
    Display_printf(display, 0, 0, "[ENTER] prettyDisplay\n\r");
    static char prettyBuf[2048];  /* static: too large for stack */
    int pos = 0;
    int linePos = 0;
    int indent = 0;
    int inString = 0;
    const char *prefix = "  ";

    for (int k = 0; prefix[k] != '\0'; k++) { prettyBuf[pos++] = prefix[k]; linePos++; }

    for (uint16_t i = 0; json[i] != '\0'; i++)
    {
        char c = json[i];
        if (c == '"') inString = !inString;

        if (!inString)
        {
            if (c == '{' || c == '[')
            {
                prettyBuf[pos++] = c;
                prettyBuf[pos++] = '\r';
                prettyBuf[pos++] = '\n';
                linePos = 0;
                indent++;
                for (int j = 0; j < indent; j++) { prettyBuf[pos++] = ' '; linePos++; }
                for (int k = 0; prefix[k] != '\0'; k++) { prettyBuf[pos++] = prefix[k]; linePos++; }
                continue;
            }
            else if (c == '}' || c == ']')
            {
                prettyBuf[pos++] = '\r';
                prettyBuf[pos++] = '\n';
                linePos = 0;
                indent--;
                for (int j = 0; j < indent; j++) { prettyBuf[pos++] = ' '; linePos++; }
                for (int k = 0; prefix[k] != '\0'; k++) { prettyBuf[pos++] = prefix[k]; linePos++; }
                prettyBuf[pos++] = c;
                linePos++;
                continue;
            }
            else if (c == ',')
            {
                prettyBuf[pos++] = c;
                prettyBuf[pos++] = '\r';
                prettyBuf[pos++] = '\n';
                linePos = 0;
                for (int j = 0; j < indent; j++) { prettyBuf[pos++] = ' '; linePos++; }
                for (int k = 0; prefix[k] != '\0'; k++) { prettyBuf[pos++] = prefix[k]; linePos++; }
                continue;
            }

            if (linePos >= MAX_LINE_WIDTH)
            {
                prettyBuf[pos++] = '\r';
                prettyBuf[pos++] = '\n';
                linePos = 0;
                for (int k = 0; prefix[k] != '\0'; k++) { prettyBuf[pos++] = prefix[k]; linePos++; }
            }
        }

        prettyBuf[pos++] = c;
        linePos++;
    }

    prettyBuf[pos++] = '\r';
    prettyBuf[pos++] = '\n';
    prettyBuf[pos]   = '\0';
    Display_printf(display, 0, 0, "%s", prettyBuf);
}

void createTemplate(Json_Filename_t *pTemplateBuff, Json_Handle *pTemplateHandle)
{
    Display_printf(display, 0, 0, "[ENTER] createTemplate\n\r");
    int16_t retVal;
    retVal = Json_createTemplate(pTemplateHandle, pTemplateBuff->fileBuffer, pTemplateBuff->fileSize);
    if(retVal < 0)
        Display_printf(display, 0, 0, "Error: %d, Couldn't create template \n\r", retVal);
    else
        Display_printf(display, 0, 0, "Template object created successfully. \n\r");
}

void createObject(Json_Handle tmplHandle, Json_Handle *pObjHandle, uint16_t size)
{
    Display_printf(display, 0, 0, "[ENTER] createObject\n\r");
    int16_t retVal;
    retVal = Json_createObject(pObjHandle, tmplHandle, size);
    if(retVal < 0)
        Display_printf(display, 0, 0, "Error: %d, Couldn't create json object \n\r", retVal);
    else
        Display_printf(display, 0, 0, "Json object created successfully. \n\r");
}

void parse(Json_Handle objHandle, Json_Filename_t *pJsonBuff)
{
    Display_printf(display, 0, 0, "[ENTER] parse\n\r");
    int16_t retVal;
    Display_printf(display, 0, 0, "jsonBuffer before parsing: [%s]\n\r", pJsonBuff->fileBuffer);
    Display_printf(display, 0, 0, "strlen before parsing: %d\n\r", strlen(pJsonBuff->fileBuffer));
    retVal = Json_parse(objHandle, pJsonBuff->fileBuffer, strlen(pJsonBuff->fileBuffer));
    Display_printf(display, 0, 0, "Json_parse ret: %d\n\r", retVal);
    if(retVal < 0)
        Display_printf(display, 0, 0, "Error: %d, Couldn't parse the Json file \n\r", retVal);
    else
        Display_printf(display, 0, 0, "Json was parsed successfully \n\r");
}

void getValue(char* pKey)
{
    Display_printf(display, 0, 0, "[ENTER] getValue\n\r");
    int16_t retVal;
    int32_t numValue;
    uint16_t valueSize = 32;

    retVal = Json_getValue(jsonObjHandle, pKey, &numValue, &valueSize);
    if(retVal == JSON_RC__VALUE_IS_NULL)
        Display_printf(display, 0, 0, "The value is null\n\r");
    else if(retVal < 0)
        Display_printf(display, 0, 0, "Error: %d, Couldn't get the data \n\r", retVal);
    else
        Display_printf(display, 0, 0, "The value is : %s \n\r", ((uint8_t)numValue == 0) ? "false" : "true");
}

int checkCreds(const char *filename)
{
    Display_printf(display, 0, 0, "[ENTER] checkCreds\n\r");
    Json_Filename_t tempBuff;
    int16_t retVal;
    int32_t fileHandle;
    int32_t bytesRead;
    SlFsFileInfo_t fileInfo;

    Json_Handle storedObj = 0;
    uint16_t parseObjSize = (objSize == 0) ? JSON_DEFAULT_SIZE : objSize;

    char userNew[32]    = {0};
    char passNew[32]    = {0};
    char userStored[32] = {0};
    char passStored[32] = {0};

    uint16_t sizeUser;
    uint16_t sizePass;

    if (sl_FsGetInfo((unsigned char *)filename, 0, &fileInfo) < 0) {
        Display_printf(display, 0, 0, "%s does not exist\n", filename);
        return 0;
    }
    if (fileInfo.Len == 0) {
        Display_printf(display, 0, 0, "%s is empty\n", filename);
        return 1;
    }
    if (fileInfo.Len >= MAX_SIZE) {
        Display_printf(display, 0, 0, "%s too large to read safely\n", filename);
        return -3;
    }

    Display_printf(display, 0, 0, "%s is being used\n", filename);

    sizeUser = sizeof(userNew);
    sizePass = sizeof(passNew);

    retVal = Json_getValue(jsonObjHandle, "\"user\"", userNew, &sizeUser);
    if (retVal < 0) { Display_printf(display, 0, 0, "Cannot get new username\n"); return -3; }

    retVal = Json_getValue(jsonObjHandle, "\"pword\"", passNew, &sizePass);
    if (retVal < 0) { Display_printf(display, 0, 0, "Cannot get new password\n"); return -3; }

    userNew[sizeof(userNew) - 1] = '\0';
    passNew[sizeof(passNew) - 1] = '\0';

    tempBuff.fileBuffer = setTempBuffSize;
    memset(tempBuff.fileBuffer, 0, MAX_SIZE);

    fileHandle = sl_FsOpen((unsigned char *)filename, SL_FS_READ, 0);
    if (fileHandle < 0) { Display_printf(display, 0, 0, "Cannot open stored file\n"); return -3; }

    bytesRead = sl_FsRead(fileHandle, 0, (unsigned char *)tempBuff.fileBuffer, MAX_SIZE - 1);
    if (bytesRead < 0) {
        Display_printf(display, 0, 0, "Cannot read stored file\n");
        sl_FsClose(fileHandle, NULL, NULL, 0);
        return -3;
    }

    {
        int16_t closeRet = sl_FsClose(fileHandle, NULL, NULL, 0);
        if (closeRet < 0) { Display_printf(display, 0, 0, "Cannot close stored file\n"); return -3; }
    }

    tempBuff.fileBuffer[bytesRead] = '\0';

    retVal = Json_createObject(&storedObj, templateHandle, parseObjSize);
    if (retVal < 0) { Display_printf(display, 0, 0, "Cannot create temp json object\n"); return -3; }

    retVal = Json_parse(storedObj, tempBuff.fileBuffer, (uint16_t)strlen(tempBuff.fileBuffer));
    if (retVal < 0) {
        Display_printf(display, 0, 0, "Cannot get parse new json file\n");
        Json_destroyObject(storedObj);
        return -3;
    }

    sizeUser = sizeof(userStored);
    sizePass = sizeof(passStored);

    retVal = Json_getValue(storedObj, "\"user\"", userStored, &sizeUser);
    if (retVal < 0) {
        Display_printf(display, 0, 0, "Cannot get stored username\n");
        Json_destroyObject(storedObj);
        return -3;
    }

    retVal = Json_getValue(storedObj, "\"pword\"", passStored, &sizePass);
    if (retVal < 0) {
        Display_printf(display, 0, 0, "Cannot get stored password\n");
        Json_destroyObject(storedObj);
        return -3;
    }

    userStored[sizeof(userStored) - 1] = '\0';
    passStored[sizeof(passStored) - 1] = '\0';
    Json_destroyObject(storedObj);

    Display_printf(display, 0, 0, "%s: username = %s, password = %s\n", filename, userStored, passStored);
    Display_printf(display, 0, 0, "New File: username = %s, password = %s\n", userNew, passNew);

    if (strcmp(userStored, userNew) == 0) {
        if (strcmp(passStored, passNew) == 0) {
            Display_printf(display, 0, 0, "Username and Password matched: %s\n", filename);
            return 2;
        } else {
            Display_printf(display, 0, 0, "Wrong password for: %s in %s\n", userStored, filename);
            return -2;
        }
    } else {
        Display_printf(display, 0, 0, "No matching credentials for: %s \n", filename);
        return -1;
    }
}

void storeJsonToFile(const char *filename, char *jsonStr, uint16_t size)
{
    Display_printf(display, 0, 0, "[ENTER] storeJsonToFile\n\r");
    int32_t  fileHandle;
    int32_t  Status;
    uint16_t payloadLen = (uint16_t)my_strnlen(jsonStr, size);

    if (payloadLen >= JSON_SIZE) {
        Display_printf(display, 0, 0, "storeJsonToFile: content too large for %s\n", filename);
        return;
    }

    fileHandle = sl_FsOpen((unsigned char *)filename,
                           SL_FS_CREATE | SL_FS_OVERWRITE | SL_FS_CREATE_MAX_SIZE(JSON_SIZE),
                           NULL);
    if (fileHandle < 0) {
        Display_printf(display, 0, 0, "storeJsonToFile: cannot open %s (%d)\n", filename, fileHandle);
        return;
    }

    Status = sl_FsWrite(fileHandle, 0, (unsigned char *)jsonStr, payloadLen + 1);
    if (Status < 0) {
        Display_printf(display, 0, 0, "storeJsonToFile: write failed (%d)\n", Status);
        sl_FsClose(fileHandle, NULL, NULL, 0);
        return;
    }

    Status = sl_FsClose(fileHandle, NULL, NULL, 0);
    if (Status < 0) {
        Display_printf(display, 0, 0, "storeJsonToFile: close failed (%d)\n", Status);
        return;
    }

    Display_printf(display, 0, 0, "storeJsonToFile: saved to %s\n", filename);

    /* Push to server -- only after system is fully initialised.
     * During jsonThread boot sem is held; httpPost would block on
     * network I/O while no other thread can acquire the semaphore. */
    if (systemReady)
        httpQ_post(filename);
}

/* ---------------------------------------------------------------
 * zeroJsonIntField()
 *
 * In-place helper: finds "key":N in buf and replaces the integer
 * value N (including a leading minus sign) with 0.  Does nothing
 * if the key is absent or the value is already 0.
 * Used to clear window0-3 before persisting an updateMCUProfile
 * so stored files always start a session with zeroed windows.
 * --------------------------------------------------------------- */
static void zeroJsonIntField(char *buf, const char *key)
{
    char *p = strstr(buf, key);
    if (p == NULL) return;
    p += strlen(key);
    if (*p != ':') return;
    p++;                            /* skip ':' */
    char *start = p;
    if (*p == '-') p++;             /* optional minus */
    while (*p >= '0' && *p <= '9') p++;
    size_t oldLen = (size_t)(p - start);
    if (oldLen == 1 && start[0] == '0') return;   /* already 0 */
    memmove(start + 1, p, strlen(p) + 1);          /* shift tail left */
    start[0] = '0';
}

void storeJson(char *jsonString, uint16_t size)
{
    Display_printf(display, 0, 0, "[ENTER] storeJson\n\r");
    int32_t fileHandle = 0;
    int32_t Status = 0;
    int8_t fileNum = -1;
    int8_t checkStatus;

    for (int i = 0; i < NUM_JSON_FILES; i++) {
        checkStatus = checkCreds(jsonFiles[i]);
        if (checkStatus == 2)                                   { fileNum = i; break; }
        else if (checkStatus == -2)                             { fileNum = -2; }
        else if ((checkStatus == 0 || checkStatus == 1) && fileNum == -1) { fileNum = i; }
    }

    if (fileNum == -1) { Display_printf(display, 0, 0, "No available json files. Cannot create new account"); return; }
    if (fileNum == -2) { Display_printf(display, 0, 0, "Cannot store duplicate usernames\n"); return; }

    fileHandle = sl_FsOpen((unsigned char *)jsonFiles[fileNum],
                           SL_FS_CREATE | SL_FS_OVERWRITE | SL_FS_CREATE_MAX_SIZE(JSON_SIZE), NULL);
    if (fileHandle < 0) { Display_printf(display, 0, 0, "Failed to open JSON file: %d\n", fileHandle); return; }

    {
        uint16_t payloadLen = (uint16_t)my_strnlen(jsonString, size);
        if (payloadLen >= JSON_SIZE) {
            Display_printf(display, 0, 0, "JSON content too large for file buffer\n");
            sl_FsClose(fileHandle, NULL, NULL, 0);
            return;
        }
        Status = sl_FsWrite(fileHandle, 0, (unsigned char *)jsonString, payloadLen + 1);
    }

    if (Status < 0) {
        Display_printf(display, 0, 0, "Failed to write JSON file: %d\n", Status);
        sl_FsClose(fileHandle, NULL, NULL, 0);
        return;
    }

    Status = sl_FsClose(fileHandle, NULL, NULL, 0);
    if (Status < 0)
        Display_printf(display, 0, 0, "Failed to close JSON file: %d\n", Status);
    else
        Display_printf(display, 0, 0, "JSON file saved successfully at %s.\n", jsonFiles[fileNum]);
}

void build(Json_Handle objHandle, uint16_t *pObjSize, const char *storeFilename)
{
    Display_printf(display, 0, 0, "[ENTER] build\n\r");
    char        *builtText;
    int16_t     retVal;
    char        strVal[64] = {0};
    uint16_t    strSize;
    int32_t     intVal;
    uint16_t    intSize;
    int         i;

    /* 12 string fields matching templateFile (type/uid omitted — always visible
     * in the pretty-print above; stock omitted — lives in compStock not here) */
    static const char *mcuStrKeys[] = {
        "\"user\"",    "\"pword\"",   "\"email\"",
        "\"script0\"", "\"script1\"", "\"script2\"", "\"script3\"",
        "\"device0\"", "\"device1\"", "\"device2\"", "\"device3\"", "\"device4\""
    };
    /* 9 int fields matching templateFile: dose0-3, window0-3, time.
     * stock0-3 are NOT in templateFile — they live in compStockTemplate. */
    static const char *mcuIntKeys[] = {
        "\"dose0\"",   "\"dose1\"",   "\"dose2\"",   "\"dose3\"",
        "\"window0\"", "\"window1\"", "\"window2\"", "\"window3\"",
        "\"time\""
    };
    static const char *errStrKeys[] = { "\"type\"", "\"uid\"", "\"message\"" };
    static const char *errIntKeys[] = { "\"time\"" };
    static const char *csStrKeys[]  = { "\"medicine0\"", "\"medicine1\"", "\"medicine2\"", "\"medicine3\"" };
    static const char *csIntKeys[]  = {
        /* dose0-3 are profile fields, not compStock fields -- omitted here */
        "\"stock0\"", "\"stock1\"", "\"stock2\"", "\"stock3\""
    };

    builtText = (char *)setBuildSize;

    retVal = Json_build(objHandle, builtText, pObjSize);
    if(retVal < 0) { Display_printf(display, 0, 0, "Error: %d, Couldn't build the json.\n\r", retVal); return; }

    removeUnwantedChars(builtText);
    prettyDisplay(builtText);

    switch (templateBuff.templateType)
    {
        case TEMPLATE_UPDATE_MCU:
            Display_printf(display, 0, 0, "-- String fields (UpdateMCU) --\n\r");
            for (i = 0; i < 12; i++) {  /* 12 = ARRAY_LEN(mcuStrKeys) */
                strSize = sizeof(strVal); memset(strVal, 0, sizeof(strVal));
                retVal = Json_getValue(objHandle, mcuStrKeys[i], strVal, &strSize);
                Display_printf(display, 0, 0, retVal >= 0 ? "  %-12s : %s\n\r" : "  %-12s : ERROR %d\n\r", mcuStrKeys[i], retVal >= 0 ? (intptr_t)strVal : retVal);
            }
            Display_printf(display, 0, 0, "-- Int32 fields (UpdateMCU) --\n\r");
            for (i = 0; i < 9; i++) {  /* 9 = ARRAY_LEN(mcuIntKeys) */
                intSize = sizeof(int32_t); intVal = 0;
                retVal = Json_getValue(objHandle, mcuIntKeys[i], &intVal, &intSize);
                Display_printf(display, 0, 0, retVal >= 0 ? "  %-12s : %d\n\r" : "  %-12s : ERROR %d\n\r", mcuIntKeys[i], retVal >= 0 ? intVal : retVal);
            }
            break;

        case TEMPLATE_ERROR:
            Display_printf(display, 0, 0, "-- String fields (Error) --\n\r");
            for (i = 0; i < 3; i++) {
                strSize = sizeof(strVal); memset(strVal, 0, sizeof(strVal));
                retVal = Json_getValue(objHandle, errStrKeys[i], strVal, &strSize);
                Display_printf(display, 0, 0, retVal >= 0 ? "  %-12s : %s\n\r" : "  %-12s : ERROR %d\n\r", errStrKeys[i], retVal >= 0 ? (intptr_t)strVal : retVal);
            }
            Display_printf(display, 0, 0, "-- Int32 fields (Error) --\n\r");
            intSize = sizeof(int32_t); intVal = 0;
            retVal = Json_getValue(objHandle, errIntKeys[0], &intVal, &intSize);
            Display_printf(display, 0, 0, retVal >= 0 ? "  %-12s : %d\n\r" : "  %-12s : ERROR %d\n\r", errIntKeys[0], retVal >= 0 ? intVal : retVal);
            break;

        case TEMPLATE_COMP_STOCK:
            Display_printf(display, 0, 0, "-- Medicine names (compStock) --\n\r");
            for (i = 0; i < 4; i++) {
                strSize = sizeof(strVal); memset(strVal, 0, sizeof(strVal));
                retVal = Json_getValue(objHandle, csStrKeys[i], strVal, &strSize);
                Display_printf(display, 0, 0, retVal >= 0 ? "  %-14s : %s\n\r" : "  %-14s : ERROR %d\n\r", csStrKeys[i], retVal >= 0 ? (intptr_t)strVal : retVal);
            }
            Display_printf(display, 0, 0, "-- Stock (compStock) --\n\r");
            for (i = 0; i < 4; i++) {
                intSize = sizeof(int32_t); intVal = 0;
                retVal = Json_getValue(objHandle, csIntKeys[i], &intVal, &intSize);
                Display_printf(display, 0, 0, retVal >= 0 ? "  %-14s : %d\n\r" : "  %-14s : ERROR %d\n\r", csIntKeys[i], retVal >= 0 ? intVal : retVal);
            }
            break;

        case TEMPLATE_TRUSTED_UID:
            Display_printf(display, 0, 0, "-- Trusted UIDs --\n\r");
            for (i = 0; i < TRUSTED_UID_MAX_COUNT; i++) {
                strSize = sizeof(strVal); memset(strVal, 0, sizeof(strVal));
                retVal = Json_getValue(objHandle, trustedUIDKeys[i], strVal, &strSize);
                Display_printf(display, 0, 0, retVal >= 0 ? "  %-8s : %s\n\r" : "  %-8s : ERROR %d\n\r", trustedUIDKeys[i], retVal >= 0 ? (intptr_t)strVal : retVal);
            }
            break;

        default:
            Display_printf(display, 0, 0, "build: templateType UNKNOWN - skipping field dump.\n\r");
            break;
    }

    if (storeFilename != NULL)
    {
        if (templateBuff.templateType == TEMPLATE_UPDATE_MCU)
        {
            /* Zero window0-3 before persisting — stored profiles always start
             * a session with zeroed windows; the server assigns live values on
             * the next syncProfileFromServer call.  objHandle is left unchanged
             * so the current session continues using the real window values. */
            zeroJsonIntField(builtText, "\"window0\"");
            zeroJsonIntField(builtText, "\"window1\"");
            zeroJsonIntField(builtText, "\"window2\"");
            zeroJsonIntField(builtText, "\"window3\"");
            *pObjSize = (uint16_t)strlen(builtText);
            storeJson(builtText, *pObjSize);
        }
        else
            storeJsonToFile(storeFilename, builtText, *pObjSize);
    }
}

void destroyTemplate(Json_Handle *pTemplateHandle)
{
    Display_printf(display, 0, 0, "[ENTER] destroyTemplate\n\r");
    int16_t retVal = Json_destroyTemplate(*pTemplateHandle);
    if(retVal < 0) { Display_printf(display, 0, 0, "Error: %d, Couldn't destroy the template.  \n\r", retVal); return; }
    *pTemplateHandle = 0;
    Display_printf(display, 0, 0, "Template was destroyed successfully.  \n\r");
}

void destroyJsonObject(Json_Handle *pObjHandle)
{
    Display_printf(display, 0, 0, "[ENTER] destroyJsonObject\n\r");
    int16_t retVal = Json_destroyObject(*pObjHandle);
    if(retVal < 0) { Display_printf(display, 0, 0, "Error: %d, Couldn't destroy the json.  \n\r", retVal); return; }
    *pObjHandle = 0;
    Display_printf(display, 0, 0, "Json was destroyed successfully.  \n\r");
}

void storeTemplate(Json_Filename_t *pTemplateBuff, const char *filename)
{
    Display_printf(display, 0, 0, "[ENTER] storeTemplate\n\r");
    int32_t fileHandle;
    int32_t retVal;

    fileHandle = sl_FsOpen((unsigned char *)filename,
                           SL_FS_CREATE | SL_FS_OVERWRITE | SL_FS_CREATE_MAX_SIZE(TEMPLATE_SIZE), NULL);
    if(fileHandle < 0) { Display_printf(display, 0, 0, "Failed to open template file: %d\n", fileHandle); return; }

    retVal = sl_FsWrite(fileHandle, 0, (unsigned char *)pTemplateBuff->fileBuffer, pTemplateBuff->fileSize);
    if(retVal < 0) Display_printf(display, 0, 0, "Failed to write template file: %d\n", retVal);

    retVal = sl_FsClose(fileHandle, NULL, NULL, 0);
    if(retVal < 0) Display_printf(display, 0, 0, "Failed to close template file: %d\n", retVal);
    else           Display_printf(display, 0, 0, "Template file written successfully to %s.\n", filename);
}

uint8_t compUID(char* UID, const char* filename)
{
    Display_printf(display, 0, 0, "[ENTER] compUID\n\r");
    Json_Filename_t tempBuff;
    int16_t retVal;
    int32_t fileHandle;
    int32_t bytesRead;
    SlFsFileInfo_t fileInfo;
    char storedUID[32] = {0};
    uint16_t sizeUID;

    if (sl_FsGetInfo((unsigned char *)filename, 0, &fileInfo) < 0) { Display_printf(display,0,0,"%s does not exist\n",filename); return 0; }
    if (fileInfo.Len == 0 || fileInfo.Len >= MAX_SIZE) return 0;

    tempBuff.fileBuffer = setTempBuffSize;
    memset(tempBuff.fileBuffer, 0, MAX_SIZE);

    fileHandle = sl_FsOpen((unsigned char *)filename, SL_FS_READ, 0);
    if (fileHandle < 0) { Display_printf(display,0,0,"Cannot open %s\n",filename); return 0; }

    bytesRead = sl_FsRead(fileHandle, 0, (unsigned char *)tempBuff.fileBuffer, MAX_SIZE - 1);
    sl_FsClose(fileHandle, NULL, NULL, 0);
    if (bytesRead < 0) return 0;

    tempBuff.fileBuffer[bytesRead] = '\0';

    /* Create a local template -- the global templateHandle is destroyed
     * by jsonThread before any card is ever scanned, so compUID must
     * own its template for the lifetime of this call.               */
    Json_Handle localTmpl = 0;
    Json_Handle storedObj = 0;
    retVal = Json_createTemplate(&localTmpl, templateFile.fileBuffer,
                                 (uint16_t)strlen(templateFile.fileBuffer));
    if (retVal < 0) { Display_printf(display,0,0,"compUID: template create failed %d\n\r",retVal); return 0; }

    retVal = Json_createObject(&storedObj, localTmpl, 512);
    if (retVal < 0) { Json_destroyTemplate(localTmpl); return 0; }

    retVal = Json_parse(storedObj, tempBuff.fileBuffer, (uint16_t)strlen(tempBuff.fileBuffer));
    if (retVal < 0) { Json_destroyObject(storedObj); Json_destroyTemplate(localTmpl); return 0; }

    sizeUID = sizeof(storedUID);
    retVal = Json_getValue(storedObj, "\"uid\"", storedUID, &sizeUID);
    Json_destroyObject(storedObj);
    Json_destroyTemplate(localTmpl);
    if (retVal < 0) return 0;

    storedUID[sizeof(storedUID) - 1] = '\0';

    if (strcmp(storedUID, UID) == 0) { Display_printf(display,0,0,"UID matched in %s\n",filename); return 1; }
    return 0;
}

const char* uidToFilename(char* UID)
{
    Display_printf(display, 0, 0, "[ENTER] uidToFilename\n\r");

    if (!isTrustedUID(UID))
    {
        Display_printf(display, 0, 0, "uidToFilename: UID %s not trusted -- access denied\n\r", UID);
        return NULL;
    }

    int8_t checkStatus;
    for (int i = 0; i < NUM_JSON_FILES; i++)
    {
        checkStatus = compUID(UID, jsonFiles[i]);
        if (checkStatus == 1)
        {
            Display_printf(display, 0, 0, "UID: %s\n\n\rFound at: %s\n\r", UID, jsonFiles[i]);
            return (char*)jsonFiles[i];
        }
    }
    Display_printf(display, 0, 0, "UID does not exist\n\r");
    return NULL;
}

char* grabJson(char* filename)
{
    Display_printf(display, 0, 0, "[ENTER] grabJson\n\r");
    readFile(&grabBuffer, filename);
    Display_printf(display, 0, 0, "grabJson(%s):\n\r%s\n\r", filename, grabBuffer.fileBuffer);
    return grabBuffer.fileBuffer;
}

uint16_t getJsonStrLen(const char* filename)
{
    Display_printf(display, 0, 0, "[ENTER] getJsonStrLen\n\r");
    int32_t fileHandle;
    int32_t bytesRead;
    SlFsFileInfo_t fileInfo;
    uint16_t jsonLen = 0;
    int16_t i;

    if (sl_FsGetInfo((unsigned char *)filename, 0, &fileInfo) < 0) { Display_printf(display,0,0,"getJsonStrLen: %s does not exist\n\r",filename); return 0; }
    if (fileInfo.Len == 0 || fileInfo.Len >= MAX_SIZE)             { Display_printf(display,0,0,"getJsonStrLen: %s invalid size\n\r",filename);    return 0; }

    fileHandle = sl_FsOpen((unsigned char *)filename, SL_FS_READ, 0);
    if (fileHandle < 0) { Display_printf(display,0,0,"getJsonStrLen: cannot open %s\n\r",filename); return 0; }

    static char lenBuf[MAX_SIZE];
    memset(lenBuf, 0, MAX_SIZE);
    bytesRead = sl_FsRead(fileHandle, 0, (unsigned char *)lenBuf, MAX_SIZE - 1);
    sl_FsClose(fileHandle, NULL, NULL, 0);
    if (bytesRead < 0) { Display_printf(display,0,0,"getJsonStrLen: cannot read %s\n\r",filename); return 0; }

    for (i = bytesRead - 1; i >= 0; i--)
    {
        if (lenBuf[i] == '}') { jsonLen = (uint16_t)(i + 1); break; }
    }

    Display_printf(display, 0, 0, "getJsonStrLen(%s): allocated=%d actual=%d\n\r",
                   filename, (uint16_t)fileInfo.Len, jsonLen);
    return jsonLen;
}

/* Compartment stock helpers
 *
 *  Both functions operate on the live global csObjHandle so there is
 *  no file I/O on the read path only on the write path after a
 *  successful decrement.
 */

int32_t getCompartStock(int compartment)
{
    if (compartment < 0 || compartment > 3) return -1;
    if (csObjHandle == 0) { Display_printf(display,0,0,"getCompartStock: csObjHandle not ready\n\r"); return -1; }

    int32_t  stock = 0;
    uint16_t sz    = sizeof(int32_t);
    int16_t  ret   = Json_getValue(csObjHandle, csStockKeys[compartment], &stock, &sz);
    if (ret < 0) { Display_printf(display,0,0,"getCompartStock: getValue failed %d\n\r", ret); return -1; }
    return stock;
}

int32_t getCompartDose(int compartment)
{
    /* Dose is a profile field (dose0-3 in templateFile), NOT a compStock field.
     * Read from jsonObjHandle (active user profile), not csObjHandle.           */
    if (compartment < 0 || compartment > 3) return -1;
    if (jsonObjHandle == 0) { Display_printf(display,0,0,"getCompartDose: jsonObjHandle not ready\n\r"); return -1; }

    int32_t  dose = 0;
    uint16_t sz   = sizeof(int32_t);
    int16_t  ret  = Json_getValue(jsonObjHandle, csDoseKeys[compartment], &dose, &sz);
    if (ret < 0) { Display_printf(display,0,0,"getCompartDose: getValue failed %d\n\r", ret); return -1; }
    return dose;
}

int getCompartMedicine(int compartment, char *buf, uint16_t bufLen)
{
    Display_printf(display, 0, 0, "[ENTER] getCompartMedicine\n\r");
    if (compartment < 0 || compartment > 3 || buf == NULL) return -1;
    if (csObjHandle == 0) return -1;
    memset(buf, 0, bufLen);
    int16_t ret = Json_getValue(csObjHandle, csMedicineKeys[compartment], buf, &bufLen);
    if (ret < 0) return -1;
    return 0;
}

int getScriptName(int slot, char *buf, uint16_t bufLen)
{
    Display_printf(display, 0, 0, "[ENTER] getScriptName\n\r");
    if (slot < 0 || slot >= NUM_SCRIPT_SLOTS || buf == NULL) return -1;
    if (jsonObjHandle == 0) return -1;
    memset(buf, 0, bufLen);
    int16_t ret = Json_getValue(jsonObjHandle, csScriptKeys[slot], buf, &bufLen);
    if (ret < 0) return -1;
    return 0;
}

int getDeviceName(int slot, char *buf, uint16_t bufLen)
{
    Display_printf(display, 0, 0, "[ENTER] getDeviceName\n\r");
    if (slot < 0 || slot >= NUM_DEVICE_SLOTS || buf == NULL) return -1;
    if (jsonObjHandle == 0) return -1;
    memset(buf, 0, bufLen);
    int16_t ret = Json_getValue(jsonObjHandle, csDeviceKeys[slot], buf, &bufLen);
    if (ret < 0) return -1;
    return 0;
}

int32_t getProfileDose(int slot)
{
    if (slot < 0 || slot >= NUM_SCRIPT_SLOTS) return -1;
    if (jsonObjHandle == 0) return -1;
    int32_t  dose = 0;
    uint16_t sz   = sizeof(int32_t);
    int16_t  ret  = Json_getValue(jsonObjHandle, csDoseKeys[slot], &dose, &sz);
    if (ret < 0) return -1;
    return dose;
}

/* ---------------------------------------------------------------
 * isTrustedUID
 * Returns 1 if the given UID string matches any of uid0-uid7 in
 * the live trustedObjHandle, 0 otherwise (including on error).
 * --------------------------------------------------------------- */
int isTrustedUID(const char *uid)
{
    if (uid == NULL) return 0;
    int i;
    for (i = 0; i < g_trustedUIDCount; i++)
    {
        if (strcmp(g_trustedUIDs[i], uid) == 0)
        {
            Display_printf(display, 0, 0, "isTrustedUID: %s matched uid%d\n\r", uid, i);
            return 1;
        }
    }
    Display_printf(display, 0, 0, "isTrustedUID: %s not in trusted list\n\r", uid);
    return 0;
}

/* ---------------------------------------------------------------
 * openActiveProfile / closeActiveProfile
 * Loads the logged-in user's FS file into jsonObjHandle so that
 * getScriptName() and getProfileDose() work for the session.
 * templateHandle and jsonObjHandle are both destroyed during
 * jsonThread() init, so they must be recreated here each login.
 * --------------------------------------------------------------- */
int openActiveProfile(void)
{
    if (activeFile == NULL || activeFile[0] == '\0') return -1;

    int16_t ret = Json_createTemplate(&templateHandle,
                                      templateFile.fileBuffer,
                                      (uint16_t)strlen(templateFile.fileBuffer));
    if (ret < 0)
    {
        Display_printf(display, 0, 0, "openActiveProfile: template failed %d\n\r", ret);
        return -1;
    }

    ret = Json_createObject(&jsonObjHandle, templateHandle, 512);
    if (ret < 0)
    {
        Display_printf(display, 0, 0, "openActiveProfile: object failed %d\n\r", ret);
        Json_destroyTemplate(templateHandle);
        templateHandle = 0;
        return -1;
    }

    int16_t flen = readFile(&jsonBuffer, (char *)activeFile);
    if (flen <= 0 || jsonBuffer.fileBuffer == NULL)
    {
        Display_printf(display, 0, 0, "openActiveProfile: readFile failed %d\n\r", flen);
        Json_destroyObject(jsonObjHandle);
        Json_destroyTemplate(templateHandle);
        jsonObjHandle = 0;
        templateHandle = 0;
        return -1;
    }

    ret = Json_parse(jsonObjHandle, jsonBuffer.fileBuffer,
                     (uint16_t)strlen(jsonBuffer.fileBuffer));
    if (ret < 0)
    {
        Display_printf(display, 0, 0, "openActiveProfile: parse failed %d\n\r", ret);
        Json_destroyObject(jsonObjHandle);
        Json_destroyTemplate(templateHandle);
        jsonObjHandle = 0;
        templateHandle = 0;
        return -1;
    }

    /* Extract window0-3 into session memory and zero them in the object.
     * Windows are assigned by the server and are not persisted to the FS file;
     * the server re-issues them on every profile sync. */
    {
        static const char *windowKeys[NUM_SCRIPT_SLOTS] = {
            "\"window0\"", "\"window1\"", "\"window2\"", "\"window3\""
        };
        int32_t  zero = 0;
        uint16_t sz;
        int      w;
        for (w = 0; w < NUM_SCRIPT_SLOTS; w++)
        {
            sz = sizeof(int32_t);
            g_sessionWindow[w] = 0;
            Json_getValue(jsonObjHandle, windowKeys[w], &g_sessionWindow[w], &sz);
            Json_setValue(jsonObjHandle, windowKeys[w], &zero, sizeof(int32_t));
        }
        Display_printf(display, 0, 0,
            "profile: win[%d,%d,%d,%d]\n\r",
            (int)g_sessionWindow[0], (int)g_sessionWindow[1],
            (int)g_sessionWindow[2], (int)g_sessionWindow[3]);
    }

    /* Reset per-session dispense counters */
    memset(g_dispPills, 0, sizeof(g_dispPills));

    Display_printf(display, 0, 0, "openActiveProfile: loaded %s\n\r", activeFile);
    return 0;
}

void closeActiveProfile(void)
{
    if (jsonObjHandle != 0) destroyJsonObject(&jsonObjHandle);
    if (templateHandle != 0) destroyTemplate(&templateHandle);
    memset(g_sessionWindow, 0, sizeof(g_sessionWindow));
    memset(g_dispPills,     0, sizeof(g_dispPills));
}

/* ---------------------------------------------------------------
 * getSessionWindow()
 * Returns the window value for slot received from the server on
 * login.  The web app assigns 0 when pills are accessed outside
 * the allowed time window.  Value lives in RAM only for the
 * duration of the session.
 * --------------------------------------------------------------- */
int32_t getSessionWindow(int slot)
{
    if (slot < 0 || slot >= NUM_SCRIPT_SLOTS) return -1;
    return g_sessionWindow[slot];
}

/* ---------------------------------------------------------------
 * incrementDispPills()
 * Called after each confirmed pill dispense.  Increments the
 * session counter for the given compartment, then POSTs an
 * updateWebApp payload so the server can update its stock.
 *
 * The JSON is built directly with snprintf to avoid consuming
 * a JSON library handle (limit: 4 simultaneous).
 * --------------------------------------------------------------- */
void incrementDispPills(int compartment)
{
    if (compartment < 0 || compartment >= NUM_SCRIPT_SLOTS) return;
    if (jsonObjHandle == 0) return;

    g_dispPills[compartment]++;

    /* Read UID from the active profile object */
    char     uid[32] = {0};
    uint16_t sz = sizeof(uid);
    if (Json_getValue(jsonObjHandle, "\"uid\"", uid, &sz) < 0)
    {
        Display_printf(display, 0, 0, "dispPills: no uid\n\r");
        return;
    }
    uid[sizeof(uid) - 1] = '\0';

    /* Build updateWebApp payload without allocating a JSON handle */
    static char webAppBuf[256];   /* updateWebApp payload ~120 chars */
    snprintf(webAppBuf, sizeof(webAppBuf),
        "{\"type\":\"updateWebApp\",\"uid\":\"%s\","
        "\"dispPills0\":%d,\"dispPills1\":%d,"
        "\"dispPills2\":%d,\"dispPills3\":%d}",
        uid,
        (int)g_dispPills[0], (int)g_dispPills[1],
        (int)g_dispPills[2], (int)g_dispPills[3]);

    storeJsonToFile(WEB_APP_UPDATE_FILENAME, webAppBuf, (uint16_t)strlen(webAppBuf));

    Display_printf(display, 0, 0,
        "dispPills%d:[%d,%d,%d,%d]\n\r",
        compartment,
        (int)g_dispPills[0], (int)g_dispPills[1],
        (int)g_dispPills[2], (int)g_dispPills[3]);
}

/* ---------------------------------------------------------------
 * requestServerStock()
 *
 * Posts a minimal {"type":"updateMCUStock","uid":"..."} request to
 * the server and uses the response to refresh csObjHandle and the
 * compStock FS file.
 *
 * Called once from dispense() after PWM_stop(), so the servo is
 * idle during the network round-trip.  Blocks the calling thread
 * until the HTTP worker completes (via httpQ_postGetResponse).
 *
 * compStock is written directly to FS without calling storeJsonToFile,
 * so no additional HTTP POST is triggered.
 * --------------------------------------------------------------- */
static int jsonRespValid(const char *buf);   /* defined later in this file */

/* ---------------------------------------------------------------
 * syncStockWithServer()
 *
 * POSTs the current compStock file (type "updateWebAppStock") to the
 * server and uses the server's response to refresh csObjHandle and
 * the FS file.  No separate updateMCUStock message is needed — the
 * server responds to the updateWebAppStock POST directly with its
 * authoritative stock values.
 *
 * Called from dispense() after PWM_stop(), servo idle during wait.
 * Blocks via httpQ_postGetResponse; uses shared g_httpRespBuf (safe:
 * mid-session, syncProfileFromServer / requestNewProfile are pre-session).
 * Response is written directly to FS without re-posting to avoid a loop.
 * --------------------------------------------------------------- */
void syncStockWithServer(void)
{
    if (csObjHandle == 0 || csTmplHandle == 0)
    {
        Display_printf(display, 0, 0, "stockSync: no handles\n\r");
        return;
    }

    /* POST compStock and block for the server's updated values */
    char * const respBuf = g_httpRespBuf;
    int ret = httpQ_postGetResponse(COMP_STOCK_FILENAME, respBuf, JSON_SIZE);
    if (ret != 0 || !jsonRespValid(respBuf))
    {
        Display_printf(display, 0, 0, "stockSync: no resp %d\n\r", ret);
        return;
    }

    /* Re-populate csObjHandle with the server's values */
    Json_destroyObject(csObjHandle);
    csObjHandle = 0;
    if (Json_createObject(&csObjHandle, csTmplHandle, 512) < 0)
    {
        Display_printf(display, 0, 0, "stockSync: obj fail\n\r");
        return;
    }
    if (Json_parse(csObjHandle, respBuf, (uint16_t)strlen(respBuf)) < 0)
    {
        Display_printf(display, 0, 0, "stockSync: parse fail\n\r");
        return;
    }

    /* Persist to FS directly — no storeJsonToFile to avoid re-POST */
    int32_t fh = sl_FsOpen((unsigned char *)COMP_STOCK_FILENAME,
                            SL_FS_CREATE | SL_FS_OVERWRITE |
                            SL_FS_CREATE_MAX_SIZE(JSON_SIZE), NULL);
    if (fh < 0)
    {
        Display_printf(display, 0, 0, "stockSync: open fail %d\n\r", (int)fh);
        return;
    }
    int32_t written = sl_FsWrite(fh, 0,
                                  (unsigned char *)respBuf,
                                  (uint16_t)strlen(respBuf) + 1);
    sl_FsClose(fh, NULL, NULL, 0);

    Display_printf(display, 0, 0, "stockSync: refreshed %d\n\r", (int)written);
}

/* ---------------------------------------------------------------
 * syncProfileFromServer()
 *
 * Called by RC522_senseRFID() immediately after a UID match, before
 * bootLCD is spawned.  Sends the locally stored profile file to the
 * server and overwrites the FS file with the server's response, which
 * carries live window0-3 fields.  openActiveProfile() then reads those
 * fresh fields when bootLCD calls it.
 *
 * Blocks the RFID thread via httpQ_postGetResponse() until the HTTP
 * worker completes — no queue redesign required.  The queue is always
 * empty at card-scan time (between sessions), so this executes first.
 *
 * Uses shared g_httpRespBuf (safe: pre-session, requestServerStock is
 * mid-session only).
 *
 * Returns  0  on success (FS file updated with server data).
 *         -1  on any failure (FS file left unchanged — local data used).
 * --------------------------------------------------------------- */
int syncProfileFromServer(const char *filename)
{
    if (filename == NULL)
    {
        Display_printf(display, 0, 0, "syncProfile: NULL filename\n\r");
        return -1;
    }

    /* POST current profile, block until server responds */
    char * const respBuf = g_httpRespBuf;
    int ret = httpQ_postGetResponse(filename, respBuf, JSON_SIZE);
    if (ret != 0 || !jsonRespValid(respBuf))
    {
        Display_printf(display, 0, 0, "syncProfile: no resp %d\n\r", ret);
        return -1;
    }

    /* Overwrite FS profile file with server's enriched response */
    int32_t fh = sl_FsOpen((unsigned char *)filename,
                            SL_FS_CREATE | SL_FS_OVERWRITE |
                            SL_FS_CREATE_MAX_SIZE(JSON_SIZE), NULL);
    if (fh < 0)
    {
        Display_printf(display, 0, 0, "syncProfile: open err %d\n\r", (int)fh);
        return -1;
    }
    uint16_t len   = (uint16_t)strlen(respBuf);
    int32_t  written = sl_FsWrite(fh, 0, (unsigned char *)respBuf, len + 1);
    sl_FsClose(fh, NULL, NULL, 0);

    if (written < 0)
    {
        Display_printf(display, 0, 0, "syncProfile: write err %d\n\r", (int)written);
        return -1;
    }

    Display_printf(display, 0, 0, "syncProfile: %s synced\n\r", filename);
    return 0;
}

/* ---------------------------------------------------------------
 * onlineLogin()
 *
 * Polls the server for a UID submitted via the web application.
 * POSTs the static onlineLogin FS file ({"type":"onlineLogin","uid":""})
 * and checks the response for a non-empty "uid" field.  If a UID is
 * present the matching profile is loaded (or a new profile is requested)
 * and a session is started identically to a physical RFID card scan.
 *
 * Called periodically from syncPollingThread() only when
 * sessionActive == 0.  Routes through httpQ_postGetResponse() so NWP
 * access is serialised through the HTTP worker thread.
 *
 * Returns  1  session started (activeFile and sessionActive set).
 *          0  no pending login (empty uid in response or no response).
 *         -1  HTTP failure.
 * --------------------------------------------------------------- */
int onlineLogin(void)
{
    if (sessionActive)
        return 0;

    static char loginRespBuf[JSON_SIZE];
    memset(loginRespBuf, 0, sizeof(loginRespBuf));

    int ret = httpQ_postGetResponse(ONLINE_LOGIN_FILENAME,
                                    loginRespBuf,
                                    (uint16_t)sizeof(loginRespBuf));
    /* HTTP-layer failure -- caller may attempt reconnect. */
    if (ret != 0)
    {
        Display_printf(display, 0, 0,
            "onlineLogin: HTTP error (ret=%d)\n\r", ret);
        return -1;
    }

    /* Extract "uid" value using string search -- consistent with the
     * rest of the codebase; avoids JSON handle allocation overhead.
     * An empty body or an empty "uid" field both mean the web app has
     * no pending login -- this is the normal idle state.             */
    char uid[32] = {0};
    if (loginRespBuf[0] != '\0')
    {
        const char *p = strstr(loginRespBuf, "\"uid\":\"");
        if (p != NULL)
        {
            p += 7;   /* skip past  "uid":"  */
            uint16_t i = 0;
            while (*p && *p != '"' && i < (uint16_t)(sizeof(uid) - 1))
                uid[i++] = *p++;
            uid[i] = '\0';
        }
    }

    if (uid[0] == '\0')
    {
        Display_printf(display, 0, 0, "onlineLogin: no pending login\n\r");
        return 0;
    }

    Display_printf(display, 0, 0, "onlineLogin: UID = %s\n\r", uid);

    /* Mirror the RFID login flow: look up existing profile or request one */
    const char *found = uidToFilename(uid);
    if (found != NULL)
    {
        activeFile = found;
        syncProfileFromServer(activeFile);
        sessionActive = 1;
        Display_printf(display, 0, 0,
            "onlineLogin: session started (%s)\n\r", activeFile);
    }
    else
    {
        const char *newSlot = requestNewProfile(uid);
        if (newSlot != NULL)
        {
            activeFile    = newSlot;
            sessionActive = 1;
            Display_printf(display, 0, 0,
                "onlineLogin: new profile registered (%s)\n\r", activeFile);
        }
        else
        {
            Display_printf(display, 0, 0,
                "onlineLogin: profile request failed\n\r");
            return -1;
        }
    }

    return 1;
}

/* ---------------------------------------------------------------
 * getSlotUID()
 *
 * Reads the first 256 bytes of a profile FS file and extracts
 * the "uid" string value without allocating any JSON handles.
 * The UID always appears near the front of the file so a partial
 * read is sufficient.
 *
 * Returns  0  on success (outUID filled; may be "" if field absent).
 *         -1  if the file cannot be opened or read.
 * --------------------------------------------------------------- */
static int getSlotUID(const char *filename, char *outUID, uint16_t outLen)
{
    char    peek[256];
    int32_t fh = sl_FsOpen((unsigned char *)filename, SL_FS_READ, 0);
    if (fh < 0) { outUID[0] = '\0'; return -1; }
    int32_t n  = sl_FsRead(fh, 0, (unsigned char *)peek, (uint16_t)sizeof(peek) - 1);
    sl_FsClose(fh, NULL, NULL, 0);
    if (n < 0)  { outUID[0] = '\0'; return -1; }
    peek[n] = '\0';

    const char *p = strstr(peek, "\"uid\":\"");
    if (p == NULL) { outUID[0] = '\0'; return 0; }
    p += 7;   /* skip past  "uid":"  */
    uint16_t i = 0;
    while (*p && *p != '"' && i < (uint16_t)(outLen - 1))
        outUID[i++] = *p++;
    outUID[i] = '\0';
    return 0;
}

/* ---------------------------------------------------------------
 * findFreeProfileSlot()
 *
 * Iterates json1-4 looking for a slot whose stored UID is either:
 *   (a) empty string   -- genuinely unused slot, preferred
 *   (b) not in the trusted UID list -- stale entry, acceptable
 *
 * Returns a pointer into jsonFiles[] on success, NULL if all four
 * slots are occupied by currently trusted UIDs.
 * --------------------------------------------------------------- */
static const char *findFreeProfileSlot(void)
{
    const char *fallback = NULL;   /* first stale-but-not-empty slot */
    int i;
    for (i = 0; i < NUM_JSON_FILES; i++)
    {
        char slotUID[32] = {0};
        int rc = getSlotUID(jsonFiles[i], slotUID, (uint16_t)sizeof(slotUID));
        if (rc < 0)
        {
            /* File unreadable / doesn't exist -- treat as free */
            Display_printf(display, 0, 0,
                "findFreeSlot: %s unreadable, using it\n\r", jsonFiles[i]);
            return jsonFiles[i];
        }
        if (slotUID[0] == '\0')
        {
            Display_printf(display, 0, 0,
                "findFreeSlot: %s empty, using it\n\r", jsonFiles[i]);
            return jsonFiles[i];   /* empty UID -- best choice */
        }
        if (!isTrustedUID(slotUID) && fallback == NULL)
        {
            fallback = jsonFiles[i];   /* stale: user removed from trust list */
            Display_printf(display, 0, 0,
                "findFreeSlot: %s stale (%s), noted as fallback\n\r",
                jsonFiles[i], slotUID);
        }
    }
    if (fallback)
        Display_printf(display, 0, 0,
            "findFreeSlot: using fallback %s\n\r", fallback);
    else
        Display_printf(display, 0, 0,
            "findFreeSlot: all slots occupied by trusted UIDs\n\r");
    return fallback;
}

/* ---------------------------------------------------------------
 * profileHasCredentials()
 *
 * Lightweight check that a server profile response contains
 * non-empty "user" and "pword" fields.  No JSON handles used.
 *
 * Returns 1 if both fields are present and non-empty, 0 otherwise.
 * --------------------------------------------------------------- */
static int profileHasCredentials(const char *buf)
{
    /* Locate  "user":"<value>"  -- value must not be an empty string */
    const char *u = strstr(buf, "\"user\":\"");
    if (u == NULL) return 0;
    u += 8;   /* skip past  "user":"  */
    if (*u == '"') return 0;   /* empty string */

    /* Locate  "pword":"<value>" */
    const char *p = strstr(buf, "\"pword\":\"");
    if (p == NULL) return 0;
    p += 9;   /* skip past  "pword":"  */
    if (*p == '"') return 0;   /* empty string */

    return 1;
}

/* ---------------------------------------------------------------
 * requestNewProfile()
 *
 * Called when a trusted UID has no matching local profile file.
 * Sends a minimal {"type":"updateMCUProfile","uid":"..."} to the
 * server, blocks for the response (which is a full profile), then
 * writes the response into the first available json slot.
 *
 * Rejection rules (returns NULL on any):
 *   - HTTP failure or invalid JSON response
 *   - Response missing non-empty "user" or "pword" fields
 *   - No free profile slot available (all 4 held by active UIDs)
 *   - FS write failure
 *
 * On success the response is written directly (no re-POST) and the
 * slot filename is returned.  The caller should set activeFile to
 * this value and sessionActive = 1 before spawning bootLCD.
 *
 * Uses shared g_httpRespBuf (safe: only called pre-session while
 * sessionActive == 0; requestServerStock is mid-session only).
 * --------------------------------------------------------------- */
const char *requestNewProfile(const char *uid)
{
    if (uid == NULL || uid[0] == '\0')
    {
        Display_printf(display, 0, 0, "newProfile: NULL/empty uid\n\r");
        return NULL;
    }

    /* 1. Build minimal registration request directly into g_httpRespBuf.
     *    The FS file preserves the payload string, so the buffer can be
     *    safely reused as the response buffer in step 3 — no separate
     *    static reqBuf needed, saving 128 bytes from .bss.             */
    char * const respBuf = g_httpRespBuf;
    snprintf(respBuf, JSON_SIZE,
             "{\"type\":\"updateMCUProfile\",\"uid\":\"%s\"}", uid);

    /* 2. Write request to transient FS file */
    int32_t fh = sl_FsOpen((unsigned char *)NEW_PROFILE_REQUEST_FILENAME,
                            SL_FS_CREATE | SL_FS_OVERWRITE |
                            SL_FS_CREATE_MAX_SIZE(128), NULL);
    if (fh < 0)
    {
        Display_printf(display, 0, 0, "newProfile: req open err %d\n\r", (int)fh);
        return NULL;
    }
    int32_t wr = sl_FsWrite(fh, 0, (unsigned char *)respBuf,
                             (uint16_t)strlen(respBuf) + 1);
    sl_FsClose(fh, NULL, NULL, 0);
    if (wr < 0)
    {
        Display_printf(display, 0, 0, "newProfile: req write err %d\n\r", (int)wr);
        return NULL;
    }

    /* 3. POST and block for server's full profile response.
     *    httpQ_postGetResponse reads from the FS file (not the buffer),
     *    so overwriting respBuf with the response is safe.             */
    int ret = httpQ_postGetResponse(NEW_PROFILE_REQUEST_FILENAME,
                                    respBuf, JSON_SIZE);
    if (ret != 0 || !jsonRespValid(respBuf))
    {
        Display_printf(display, 0, 0, "newProfile: no resp %d\n\r", ret);
        return NULL;
    }

    /* 4. Reject if server omitted credentials -- untrusted or unknown user */
    if (!profileHasCredentials(respBuf))
    {
        Display_printf(display, 0, 0,
            "newProfile: rejected -- no user/pword in response\n\r");
        return NULL;
    }

    /* 5. Find a free slot to persist the profile */
    const char *slot = findFreeProfileSlot();
    if (slot == NULL)
    {
        Display_printf(display, 0, 0,
            "newProfile: all profile slots occupied\n\r");
        return NULL;
    }

    /* 6. Write directly to the slot -- no re-POST (server already has data) */
    fh = sl_FsOpen((unsigned char *)slot,
                   SL_FS_CREATE | SL_FS_OVERWRITE |
                   SL_FS_CREATE_MAX_SIZE(JSON_SIZE), NULL);
    if (fh < 0)
    {
        Display_printf(display, 0, 0, "newProfile: slot open err %d\n\r", (int)fh);
        return NULL;
    }
    uint16_t len = (uint16_t)strlen(respBuf);
    wr = sl_FsWrite(fh, 0, (unsigned char *)respBuf, len + 1);
    sl_FsClose(fh, NULL, NULL, 0);
    if (wr < 0)
    {
        Display_printf(display, 0, 0, "newProfile: slot write err %d\n\r", (int)wr);
        return NULL;
    }

    Display_printf(display, 0, 0,
        "newProfile: %s registered -> %s\n\r", uid, slot);
    return slot;
}

uint8_t matchCompartments(void)
{
    Display_printf(display, 0, 0, "[ENTER] matchCompartments\n\r");
    if (csObjHandle == 0 || jsonObjHandle == 0)
    {
        Display_printf(display, 0, 0, "matchCompartments: handles not ready\n\r");
        return 0xF;
    }
    char    medicine[32]; char script[32];
    uint8_t mask = 0;
    int     c, s;
    for (c = 0; c < 4; c++)
    {
        if (getCompartMedicine(c, medicine, sizeof(medicine)) < 0) continue;
        if (medicine[0] == '\0') continue;
        for (s = 0; s < 4; s++)
        {
            if (getScriptName(s, script, sizeof(script)) < 0) continue;
            if (script[0] == '\0') continue;
            if (strcasecmp(medicine, script) == 0) { mask |= (1u << c); break; }
        }
    }
    Display_printf(display, 0, 0, "matchCompartments: mask=0x%X\n\r", mask);
    return mask;
}

int decrementCompartStockByOne(int compartment, const char *pActiveFile)
{
    Display_printf(display, 0, 0, "[ENTER] decrementCompartStockByOne\n\r");
    if (compartment < 0 || compartment > 3) return -1;
    if (csObjHandle == 0) return -1;
    int32_t stock = getCompartStock(compartment);
    if (stock <= 0) { logError(OutOfPills, pActiveFile, compartment); return -1; }
    int32_t  newStock = stock - 1;
    uint16_t sz = sizeof(int32_t);
    int16_t  ret = Json_setValue(csObjHandle, csStockKeys[compartment], &newStock, sz);
    if (ret < 0)
        return -1;
    static char builtText[JSON_SIZE];
    uint16_t builtSize = JSON_SIZE;
    memset(builtText, 0, sizeof(builtText));
    ret = Json_build(csObjHandle, builtText, &builtSize);
    if (ret < 0) return -1;
    storeJsonToFile(COMP_STOCK_FILENAME, builtText, builtSize);
    Display_printf(display, 0, 0,
        "decrementCompartStock: compartment %d  %d -> %d\n\r",
        compartment, (int)stock, (int)newStock);
    if (newStock == 0) {
        Display_printf(display, 0, 0,
            "decrementCompartStock: compartment %d now EMPTY\n\r", compartment);
        logError(OutOfPills, pActiveFile, compartment);
    }
    return 0;
}

int decrementCompartStock(int compartment, const char *pActiveFile)
{
    Display_printf(display, 0, 0, "[ENTER] decrementCompartStock\n\r");
    /* declared in error_handler.c */
    /* OutOfPills = -4 from error_handler.c */

    if (compartment < 0 || compartment > 3) return -1;
    if (csObjHandle == 0) { Display_printf(display,0,0,"decrementCompartStock: csObjHandle not ready\n\r"); return -1; }

    int32_t stock = getCompartStock(compartment);
    int32_t dose  = getCompartDose(compartment);

    if (stock <= 0 || dose < 0)
    {
        Display_printf(display, 0, 0,
            "decrementCompartStock: compartment %d already empty\n\r", compartment);
        logError(OutOfPills, pActiveFile, compartment);
        return -1;
    }

    int32_t  newStock = stock - dose;
    if (newStock < 0) newStock = 0;

    uint16_t sz  = sizeof(int32_t);
    int16_t  ret = Json_setValue(csObjHandle, csStockKeys[compartment], &newStock, sz);
    if (ret < 0)
    {
        Display_printf(display, 0, 0,
            "decrementCompartStock: setValue failed %d\n\r", ret);
        return -1;
    }

    /* Flush updated object to the FS file */
    static char builtText[JSON_SIZE];  /* static: too large for stack */
    uint16_t builtSize = JSON_SIZE;
    memset(builtText, 0, sizeof(builtText));
    ret = Json_build(csObjHandle, builtText, &builtSize);
    if (ret < 0)
    {
        Display_printf(display, 0, 0,
            "decrementCompartStock: build failed %d\n\r", ret);
        return -1;
    }

    storeJsonToFile(COMP_STOCK_FILENAME, builtText, builtSize);

    Display_printf(display, 0, 0,
        "decrementCompartStock: compartment %d  %d -> %d\n\r",
        compartment, (int)stock, (int)newStock);

    if (newStock == 0)
    {
        Display_printf(display, 0, 0,
            "decrementCompartStock: compartment %d now EMPTY\n\r", compartment);
        logError(OutOfPills, pActiveFile, compartment);
    }

    return 0;
}

/* -----------------------------------------------------------------------
 * jsonRespValid()
 *
 * Lightweight server-response sanity check -- no JSON handles allocated.
 * Returns 1 if buf looks like a valid JSON object with a "type" field,
 * 0 otherwise.
 * --------------------------------------------------------------------- */
static int jsonRespValid(const char *buf)
{
    if (buf == NULL || buf[0] == '\0') return 0;
    const char *p = buf;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return (*p == '{'                          &&
            strstr(buf, "\"type\":") != NULL   &&
            strchr(buf, '}')         != NULL);
}

/* -----------------------------------------------------------------------
 * loadDummyFiles()
 *
 * Writes the compile-time default JSON strings to the SimpleLink FS.
 * Called once at boot to pre-populate the device with example data:
 *   - json1, json2, json3, json4       (UpdateMCU profiles)
 *   - compStock                        (compartment stock)
 *   - trustedUIDs                      (authorized cards)
 *
 * Skips any file that already exists on the FS to avoid overwriting
 * user data or server-synced updates.
 *
 * Returns 0 on success, -1 if any file write fails.
 * --------------------------------------------------------------------- */

int loadDummyFiles(void)
{
    Display_printf(display, 0, 0, "[ENTER] loadDummyFiles\n\r");

    SlFsFileInfo_t fsInfo;
    int32_t fileHandle;
    int32_t written;
    int status = 0;

    /* Define dummy file mappings: filename, json string, size */
    struct {
        const char *filename;
        const char *jsonStr;
        uint16_t    size;
    } dummyFiles[] = {
        { JSON_FILENAME1,        jsonString,             (uint16_t)strlen(jsonString) },
        { COMP_STOCK_FILENAME,   compStockJsonString,   (uint16_t)strlen(compStockJsonString) },
        { TRUSTED_UID_FILENAME,  trustedUIDJsonString,  (uint16_t)strlen(trustedUIDJsonString) },
        { ONLINE_LOGIN_FILENAME, onlineLoginJsonString, (uint16_t)strlen(onlineLoginJsonString) },
    };

    int numFiles = sizeof(dummyFiles) / sizeof(dummyFiles[0]);
    int i;

    Display_printf(display, 0, 0,
        "\r\n========================================\n\r");
    Display_printf(display, 0, 0, "  LOADING DUMMY FILES\n\r");
    Display_printf(display, 0, 0,
        "========================================\n\r");

    for (i = 0; i < numFiles; i++)
    {
        const char *filename = dummyFiles[i].filename;
        const char *jsonStr  = dummyFiles[i].jsonStr;
        uint16_t    size     = dummyFiles[i].size;

        /* Check if file already exists */
        if (sl_FsGetInfo((unsigned char *)filename, 0, &fsInfo) >= 0)
        {
            Display_printf(display, 0, 0,
                "  %s: already exists (size=%u), skipping\n\r",
                filename, (unsigned)fsInfo.Len);
            continue;
        }

        /* File does not exist -- create and write */
        Display_printf(display, 0, 0,
            "  %s: creating...\n\r", filename);

        fileHandle = sl_FsOpen((unsigned char *)filename,
                               SL_FS_CREATE | SL_FS_OVERWRITE |
                               SL_FS_CREATE_MAX_SIZE(JSON_SIZE),
                               NULL);
        if (fileHandle < 0)
        {
            Display_printf(display, 0, 0,
                "  %s: open failed, ret=%d\n\r", filename, (int)fileHandle);
            status = -1;
            continue;
        }

        written = sl_FsWrite(fileHandle, 0,
                            (unsigned char *)jsonStr, size + 1);
        sl_FsClose(fileHandle, NULL, NULL, 0);

        if (written < 0)
        {
            Display_printf(display, 0, 0,
                "  %s: write failed, ret=%d\n\r", filename, (int)written);
            status = -1;
            continue;
        }

        Display_printf(display, 0, 0,
            "  %s: written (%u bytes)\n\r", filename, size);
    }

    Display_printf(display, 0, 0,
        "========================================\n\r");
    Display_printf(display, 0, 0,
        "  DUMMY FILE LOAD %s\n\r",
        (status == 0) ? "COMPLETE" : "COMPLETE (with errors)");
    Display_printf(display, 0, 0,
        "========================================\n\r");

    return status;
}

/* -----------------------------------------------------------------------
 * jsonThread()
 *
 * Runs inline inside schedFuncs() while the boot semaphore is held.
 * httpWorkerThread is NOT yet spawned, so httpPostGetResponse() is called
 * directly here (bypasses the queue -- safe because no other thread can
 * access the NWP while sem is held).
 *
 * For each file (json1, compStock, trustedUIDs) the priority is:
 *   1. Fresh response from the web application  (server wins)
 *   2. Previously stored FS file               (fallback on no connection
 *                                               or invalid response)
 *   3. Hardcoded firmware default              (first boot only)
 * --------------------------------------------------------------------- */
void* jsonThread(void *arg)
{
    Display_printf(display, 0, 0, "[ENTER] jsonThread\n\r");

    static char    respBuf[JSON_SIZE];   /* server response -- static: too large for stack */
    SlFsFileInfo_t fsInfo;
    int            httpRet;

    /*load example files into FS*/
    loadDummyFiles();

    /* ----------------------------------------------------------------
     * UpdateMCU -- user profile (json1)
     * ---------------------------------------------------------------- */
    {
        templateBuff = templateFile;
        Json_destroyTemplate(templateHandle);   /* clear any stale handle */

        createTemplate(&templateBuff, &templateHandle);
        storeTemplate(&templateBuff, TEMPLATE_FILENAME);
        templateSize = readFile(&templateBuff, TEMPLATE_FILENAME);

        createObject(templateHandle, &jsonObjHandle, 512);

        /* 1. Try server */
        memset(respBuf, 0, sizeof(respBuf));
        httpRet = httpPostGetResponse(JSON_FILENAME1, respBuf,
                                      (uint16_t)sizeof(respBuf));
        if (httpRet == 0 && jsonRespValid(respBuf))
        {
            Display_printf(display, 0, 0,
                "jsonThread: json1 -- server response accepted\n\r");
            jsonBuffer.fileBuffer = respBuf;
            jsonBuffer.fileSize   = (int16_t)strlen(respBuf);
        }
        /* 2. Fall back to stored FS file */
        else if (sl_FsGetInfo((unsigned char *)JSON_FILENAME1, 0, &fsInfo) >= 0)
        {
            Display_printf(display, 0, 0,
                "jsonThread: json1 -- using stored FS file (HTTP ret=%d)\n\r",
                httpRet);
            readFile(&jsonBuffer, JSON_FILENAME1);
        }
        /* 3. First-boot default */
        else
        {
            Display_printf(display, 0, 0,
                "jsonThread: json1 -- no FS file, using firmware default\n\r");
            jsonBuffer.fileBuffer = (char *)jsonString;
            jsonBuffer.fileSize   = (int16_t)strlen(jsonString);
        }

        parse(jsonObjHandle, &jsonBuffer);
        build(jsonObjHandle, &objSize, JSON_FILENAME1);

        destroyJsonObject(&jsonObjHandle);
        destroyTemplate(&templateHandle);
        templateHandle = 0;
    }

    /* ----------------------------------------------------------------
     * compStock
     * csObjHandle and csTmplHandle are intentionally left open after
     * this block so getCompartStock() / decrementCompartStock() work
     * for the entire runtime.
     * ---------------------------------------------------------------- */
    {
        Json_Filename_t csTmplBuff = compStockTemplate;
        uint16_t        csObjSize  = 512;

        templateBuff = compStockTemplate;   /* must be set before build() */
        createTemplate(&csTmplBuff, &csTmplHandle);
        storeTemplate(&csTmplBuff, COMP_STOCK_TEMPLATE);

        createObject(csTmplHandle, &csObjHandle, csObjSize);

        /* 1. Try server */
        memset(respBuf, 0, sizeof(respBuf));
        httpRet = httpPostGetResponse(COMP_STOCK_FILENAME, respBuf, (uint16_t)sizeof(respBuf));
        if (httpRet == 0 && jsonRespValid(respBuf))
        {
            Display_printf(display, 0, 0,
                "jsonThread: compStock -- server response accepted\n\r");
            jsonBuffer.fileBuffer = respBuf;
            jsonBuffer.fileSize   = (int16_t)strlen(respBuf);
        }
        /* 2. Fall back to stored FS file */
        else if (sl_FsGetInfo((unsigned char *)COMP_STOCK_FILENAME, 0, &fsInfo) >= 0)
        {
            Display_printf(display, 0, 0,
                "jsonThread: compStock -- using stored FS file (HTTP ret=%d)\n\r", httpRet);
            readFile(&jsonBuffer, COMP_STOCK_FILENAME);
        }
        /* 3. First-boot default */
        else
        {
            Display_printf(display, 0, 0,
                "jsonThread: compStock -- no FS file, using firmware default\n\r");
            jsonBuffer.fileBuffer = (char *)compStockJsonString;
            jsonBuffer.fileSize   = (int16_t)strlen(compStockJsonString);
        }

        parse(csObjHandle, &jsonBuffer);
        build(csObjHandle, &csObjSize, COMP_STOCK_FILENAME);
        /* handles left open intentionally */
    }

    /* ----------------------------------------------------------------
     * trustedUIDs
     * Handles are destroyed immediately after the UID cache is filled
     * to stay within the SimpleLink JSON library's simultaneous handle
     * limit (≤4).  isTrustedUID() reads from g_trustedUIDs[], not from
     * any JSON handle.
     * ---------------------------------------------------------------- */
    {
        Json_Filename_t trustedTmplBuff = trustedUIDTemplate;
        uint16_t        trustedObjSize  = 512;

        templateBuff = trustedUIDTemplate;  /* must be set before build() */
        createTemplate(&trustedTmplBuff, &trustedTmplHandle);
        storeTemplate(&trustedTmplBuff, TRUSTED_UID_TEMPLATE);

        createObject(trustedTmplHandle, &trustedObjHandle, trustedObjSize);

        /* 1. Try server */
        memset(respBuf, 0, sizeof(respBuf));
        httpRet = httpPostGetResponse(TRUSTED_UID_FILENAME, respBuf,
                                      (uint16_t)sizeof(respBuf));
        if (httpRet == 0 && jsonRespValid(respBuf))
        {
            Display_printf(display, 0, 0,
                "jsonThread: trustedUIDs -- server response accepted\n\r");
            jsonBuffer.fileBuffer = respBuf;
            jsonBuffer.fileSize   = (int16_t)strlen(respBuf);
        }
        /* 2. Fall back to stored FS file */
        else if (sl_FsGetInfo((unsigned char *)TRUSTED_UID_FILENAME, 0, &fsInfo) >= 0)
        {
            Display_printf(display, 0, 0,
                "jsonThread: trustedUIDs -- using stored FS file (HTTP ret=%d)\n\r", httpRet);
            readFile(&jsonBuffer, TRUSTED_UID_FILENAME);
        }
        /* 3. First-boot default */
        else
        {
            Display_printf(display, 0, 0, "jsonThread: trustedUIDs -- no FS file, using firmware default\n\r");
            jsonBuffer.fileBuffer = (char *)trustedUIDJsonString;
            jsonBuffer.fileSize   = (int16_t)strlen(trustedUIDJsonString);
        }

        parse(trustedObjHandle, &jsonBuffer);
        build(trustedObjHandle, &trustedObjSize, TRUSTED_UID_FILENAME);

        /* Copy uid0-uid7 into the static cache */
        g_trustedUIDCount = 0;
        int s;
        for (s = 0; s < TRUSTED_UID_MAX_COUNT; s++)
        {
            char tmp[32] = {0};
            uint16_t sz  = sizeof(tmp);
            if (Json_getValue(trustedObjHandle, trustedUIDKeys[s], tmp, &sz) >= 0
                && tmp[0] != '\0')
            {
                strncpy(g_trustedUIDs[g_trustedUIDCount], tmp,
                        sizeof(g_trustedUIDs[0]) - 1);
                g_trustedUIDs[g_trustedUIDCount][sizeof(g_trustedUIDs[0]) - 1] = '\0';
                g_trustedUIDCount++;
            }
        }
        Display_printf(display, 0, 0,
            "trustedUIDs: cached %d UID(s)\n\r", g_trustedUIDCount);

        destroyJsonObject(&trustedObjHandle);
        destroyTemplate(&trustedTmplHandle);
    }

    systemReady = 1;   /* now safe for storeJsonToFile to call httpQ_post */
    return NULL;
}
