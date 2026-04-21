#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include "json_make.h"
#include "error_handler.h"
#include "timeKeep.h"
#include "http_queue.h"

extern Display_Handle display;

error_Code errorState;

/* ================================================================
 *  genErrName()
 *
 *  Scan error0-error99 and return the first free slot name.
 *  Returns the slot index on success, -1 if all slots are occupied.
 * ================================================================ */
static int genErrName(char *outName, size_t outLen)
{
    Display_printf(display, 0, 0, "[ENTER] genErrName\n\r");
    char           candidate[16];
    int32_t        status;
    SlFsFileInfo_t fsInfo;
    int            i;

    for (i = 0; i < MAX_ERROR_FILES; i++)
    {
        snprintf(candidate, sizeof(candidate), "%s%d", ERROR_BASE_NAME, i);
        status = sl_FsGetInfo((unsigned char *)candidate, 0, &fsInfo);
        if (status < 0)
        {
            snprintf(outName, outLen, "%s", candidate);
            Display_printf(display, 0, 0,
                "genErrName: free slot -> %s\n\r", outName);
            return i;
        }
    }

    Display_printf(display, 0, 0,
        "genErrName: all %d slots occupied\n\r", MAX_ERROR_FILES);
    return -1;
}

/* ================================================================
 *  errorMessage()  --  map code to human-readable string
 * ================================================================ */
static const char *errorMessage(int retCode)
{
    Display_printf(display, 0, 0, "[ENTER] errorMessage\n\r");
    switch (retCode)
    {
        case DispensingError: return "Dispensing mechanism jammed";
        case OutOfPills:      return "Compartment out of pills";
        case FatalParsing:    return "Fatal JSON parse error - requesting resend";
        default:              return "Unknown error";
    }
}

/* ================================================================
 *  parseErrorMessage()
 *
 *  1. Read user credentials from pActiveFile.
 *  2. Build an error JSON (errorTemplate).
 *  3. For each notification device, find a free error slot and call
 *     storeJsonToFile() -- which writes to FS AND calls httpPost()
 *     automatically, so no manual POST call is needed here.
 *
 *  compartment is appended to the message string when >= 0.
 * ================================================================ */

void parseErrorMessage(int retCode, const char *pActiveFile, int compartment)
{
    Display_printf(display, 0, 0, "[ENTER] parseErrorMessage\n\r");
    Json_Handle     errTmplHandle = 0;
    Json_Handle     errObjHandle  = 0;
    Json_Filename_t errTmplBuff;

    char uidVal[32] = {0};

    uint16_t sz;
    int16_t  retVal;

    /* ---- 1. Read UID from the stored user file ---- */
    if (pActiveFile == NULL)
    {
        Display_printf(display, 0, 0,
            "parseErrorMessage: no active file, skipping\n\r");
        return;
    }
    /*Create handle dynamiclly to reduce the amount existing at once*/
    {
        static char      fileBuf[MAX_SIZE];
        Json_Handle      storedObj  = 0;
        Json_Handle      storedTmpl = 0;
        Json_Filename_t  storedTmplBuff;
        int32_t          fh;
        int32_t          bytesRead;
        SlFsFileInfo_t   fileInfo;

        memset(fileBuf, 0, MAX_SIZE);

        if (sl_FsGetInfo((unsigned char *)pActiveFile, 0, &fileInfo) < 0 ||
            fileInfo.Len == 0 || fileInfo.Len >= MAX_SIZE)
        {
            Display_printf(display, 0, 0,
                "parseErrorMessage: cannot stat %s\n\r", pActiveFile);
            return;
        }

        fh = sl_FsOpen((unsigned char *)pActiveFile, SL_FS_READ, 0);
        if (fh < 0)
        {
            Display_printf(display, 0, 0,
                "parseErrorMessage: cannot open %s\n\r", pActiveFile);
            return;
        }

        bytesRead = sl_FsRead(fh, 0,
                              (unsigned char *)fileBuf, MAX_SIZE - 1);
        sl_FsClose(fh, NULL, NULL, 0);

        if (bytesRead < 0)
        {
            Display_printf(display, 0, 0,
                "parseErrorMessage: read failed on %s\n\r", pActiveFile);
            return;
        }
        fileBuf[bytesRead] = '\0';

        storedTmplBuff = templateFile;
        createTemplate(&storedTmplBuff, &storedTmpl);

        retVal = Json_createObject(&storedObj, storedTmpl, 512);
        if (retVal < 0)
        {
            Json_destroyTemplate(storedTmpl);
            return;
        }

        retVal = Json_parse(storedObj, fileBuf, (uint16_t)strlen(fileBuf));
        if (retVal < 0)
        {
            Json_destroyObject(storedObj);
            Json_destroyTemplate(storedTmpl);
            return;
        }

        sz = sizeof(uidVal);
        Json_getValue(storedObj, "\"uid\"", uidVal, &sz);

        uidVal[sizeof(uidVal) - 1] = '\0';

        Json_destroyObject(storedObj);
        Json_destroyTemplate(storedTmpl);
    }

    /* ---- 2. Build the error JSON ---- */
    errTmplBuff = errorTemplate;
    createTemplate(&errTmplBuff, &errTmplHandle);

    retVal = Json_createObject(&errObjHandle, errTmplHandle, 512);
    if (retVal < 0)
    {
        Json_destroyTemplate(errTmplHandle);
        return;
    }

    char    typeStr[8]    = "Error";
    char    messageStr[80] = {0};
    int32_t timeVal        = (int32_t)TimeKeeper_getTime();

    /* Append compartment index to message when relevant */
    if (compartment >= 0)
        snprintf(messageStr, sizeof(messageStr), "%s (compartment %d)",
                 errorMessage(retCode), compartment);
    else
        snprintf(messageStr, sizeof(messageStr), "%s", errorMessage(retCode));

    Json_setValue(errObjHandle, "\"type\"",    typeStr,    (uint16_t)strlen(typeStr)    + 1);
    Json_setValue(errObjHandle, "\"uid\"",     uidVal,     (uint16_t)strlen(uidVal)     + 1);
    Json_setValue(errObjHandle, "\"message\"", messageStr, (uint16_t)strlen(messageStr) + 1);
    Json_setValue(errObjHandle, "\"time\"",    &timeVal,   sizeof(int32_t));

    static char builtText[JSON_SIZE];
    memset(builtText, 0, JSON_SIZE);
    uint16_t builtSize = JSON_SIZE;
    retVal = Json_build(errObjHandle, builtText, &builtSize);

    Json_destroyObject(errObjHandle);
    Json_destroyTemplate(errTmplHandle);

    if (retVal < 0)
    {
        Display_printf(display, 0, 0,
            "parseErrorMessage: build failed %d\n\r", retVal);
        return;
    }

    /* ---- 3. Store one error packet -- web app handles device routing ---- */
    {
        char errFilename[16];

        if (genErrName(errFilename, sizeof(errFilename)) >= 0)
        {
            storeJsonToFile(errFilename, builtText, builtSize);
            Display_printf(display, 0, 0,
                "parseErrorMessage: stored -> %s\n\r", errFilename);
        }
        else
        {
            Display_printf(display, 0, 0,
                "parseErrorMessage: no free error slots\n\r");
        }
    }
}

/* ================================================================
 *  logError()
 * ================================================================ */
void logError(int retCode, const char *pActiveFile, int compartment)
{
    Display_printf(display, 0, 0, "[ENTER] logError\n\r");
    errorState = (error_Code)retCode;

    switch (retCode)
    {
        case DispensingError:
            Display_printf(display, 0, 0,
                "logError: dispensing jam (compartment %d)\n\r", compartment);
            parseErrorMessage(retCode, pActiveFile, compartment);
            break;

        case OutOfPills:
            Display_printf(display, 0, 0,
                "logError: out of pills (compartment %d)\n\r", compartment);
            parseErrorMessage(retCode, pActiveFile, compartment);
            break;

        case FatalParsing:
            Display_printf(display, 0, 0,
                "logError: fatal parse error\n\r");
            parseErrorMessage(retCode, pActiveFile, compartment);
            break;

        case Disconnected:
            Display_printf(display, 0, 0,
                "logError: disconnected (LCD only)\n\r");
            /* TODO: display disconnected icon on LCD */
            break;

        case Connected:
            /* TODO: display connected icon on LCD */
            break;

        default:
            Display_printf(display, 0, 0,
                "logError: unknown code %d\n\r", retCode);
            break;
    }
}

/* ================================================================
 *  flushErrorBuffer()
 *
 *  Scans every slot error0..error99 on the SimpleLink FS.
 *  For each file found:
 *    1. POSTs the file contents to the web application via
 *       httpPostGetResponse() which leaves the FS file untouched.
 *    2. Parses the response JSON for a "return_code" int32 field.
 *    3. If return_code == 0 the server acknowledged the error --
 *       the file is deleted from the FS.
 *    4. Any other return_code or a network failure leaves the file
 *       in place so it can be retried on the next call.
 *
 *  Call this periodically (e.g. on session start, after SNTP sync)
 *  to drain acknowledged error logs from the device.
 * ================================================================ */
void flushErrorBuffer(void)
{
    Display_printf(display, 0, 0, "[ENTER] flushErrorBuffer\n\r");
    char           candidate[16];
    char           respBuf[JSON_SIZE];
    SlFsFileInfo_t fsInfo;
    int            i;
    int            flushed  = 0;
    int            retained = 0;
    int            empty    = 0;

    Display_printf(display, 0, 0,
        "\r\n========================================\n\r");
    Display_printf(display, 0, 0, "  FLUSHING ERROR BUFFER\n\r");
    Display_printf(display, 0, 0,
        "========================================\n\r");

    for (i = 0; i < MAX_ERROR_FILES; i++)
    {
        snprintf(candidate, sizeof(candidate), "%s%d", ERROR_BASE_NAME, i);

        if (sl_FsGetInfo((unsigned char *)candidate, 0, &fsInfo) < 0)
        {
            empty++;
            continue;   /* slot does not exist -- skip */
        }

        Display_printf(display, 0, 0,
            "flushErrorBuffer: posting %s\n\r", candidate);

        int ret = httpQ_postGetResponse(candidate, respBuf, (uint16_t)sizeof(respBuf));
        if (ret < 0)
        {
            Display_printf(display, 0, 0,
                "flushErrorBuffer: POST failed for %s (ret=%d) -- retained\n\r",
                candidate, ret);
            retained++;
            continue;
        }

        /* Parse response for "return_code" field using the error template */
        Json_Handle tmpl = 0;
        Json_Handle obj  = 0;
        int16_t     retVal;

        /* Build a minimal template that has return_code as int32 */
        const char *rcTemplate = "{\"return_code\":int32}";
        retVal = Json_createTemplate(&tmpl, rcTemplate,
                                     (uint16_t)strlen(rcTemplate));
        if (retVal < 0)
        {
            Display_printf(display, 0, 0,
                "flushErrorBuffer: template create failed %d\n\r", retVal);
            retained++;
            continue;
        }

        retVal = Json_createObject(&obj, tmpl, 128);
        if (retVal < 0)
        {
            Json_destroyTemplate(tmpl);
            retained++;
            continue;
        }

        retVal = Json_parse(obj, respBuf, (uint16_t)strlen(respBuf));
        if (retVal < 0)
        {
            Display_printf(display, 0, 0,
                "flushErrorBuffer: response parse failed %d -- retained\n\r",
                retVal);
            Json_destroyObject(obj);
            Json_destroyTemplate(tmpl);
            retained++;
            continue;
        }

        int32_t  returnCode = -1;
        uint16_t sz         = sizeof(int32_t);
        Json_getValue(obj, "\"return_code\"", &returnCode, &sz);
        Json_destroyObject(obj);
        Json_destroyTemplate(tmpl);

        if (returnCode == 0)
        {
            /* Server acknowledged -- delete the file */
            int32_t delRet = sl_FsDel((unsigned char *)candidate, 0);
            if (delRet >= 0)
            {
                Display_printf(display, 0, 0,
                    "flushErrorBuffer: %s acknowledged and deleted\n\r",
                    candidate);
                flushed++;
            }
            else
            {
                Display_printf(display, 0, 0,
                    "flushErrorBuffer: delete failed for %s (ret=%d)\n\r",
                    candidate, (int)delRet);
                retained++;
            }
        }
        else
        {
            Display_printf(display, 0, 0,
                "flushErrorBuffer: %s retained (return_code=%d)\n\r",
                candidate, (int)returnCode);
            retained++;
        }
    }

    Display_printf(display, 0, 0,
        "flushErrorBuffer: done -- flushed=%d  retained=%d  empty=%d\n\r",
        flushed, retained, empty);
    Display_printf(display, 0, 0,
        "========================================\n\r");
}
