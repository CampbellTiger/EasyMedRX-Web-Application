#include <string.h>
#include <unistd.h>
#include <ti/display/Display.h>
#include <ti/net/http/httpclient.h>
#include <ti/drivers/SPI.h>
#include <stdio.h>
#include <ti/utils/json/json.h>
#include "json_make.h"
#include "platform.h"
#include "error_handler.h"

#define HOSTNAME          "https://easymedrx.local:8443"
#define REQUEST_URL       "/api/device/push"
#define USER_AGENT        "HTTPClient (ARM; TI-RTOS)"
#define HTTP_RECV_BUF     (256)
#define ROOT_CA_FILE      "ca.pem"

#define TYPE_UPDATE_MCU   "updateMCU"   /* value that triggers MCU update mode */

extern Display_Handle display;
extern void printError(char *errString, int code);
extern void updateWifiIcon(uint8_t connected);   /* LCD_Menu.c */
extern SPI_Handle spi;                           /* drive_gpio.c -- NULL until configSPI() runs */

static char httpRespBuf[JSON_SIZE];

static int readTypeField(const char *filename, char *outBuf, uint16_t bufLen)
{
    Display_printf(display, 0, 0, "[ENTER] readTypeField\n\r");
    static char readBuf[MAX_SIZE];
    SlFsFileInfo_t fileInfo;
    int32_t        fh;
    int32_t        bytesRead;

    memset(outBuf, 0, bufLen);
    memset(readBuf, 0, sizeof(readBuf));

    if (sl_FsGetInfo((unsigned char *)filename, 0, &fileInfo) < 0)
    {
        Display_printf(display, 0, 0,
            "readTypeField: %s not found\n\r", filename);
        return -1;
    }

    if (fileInfo.Len == 0 || fileInfo.Len >= MAX_SIZE)
    {
        Display_printf(display, 0, 0,
            "readTypeField: %s invalid size\n\r", filename);
        return -1;
    }

    fh = sl_FsOpen((unsigned char *)filename, SL_FS_READ, 0);
    if (fh < 0)
    {
        Display_printf(display, 0, 0,
            "readTypeField: cannot open %s\n\r", filename);
        return -1;
    }

    bytesRead = sl_FsRead(fh, 0, (unsigned char *)readBuf, MAX_SIZE - 1);
    sl_FsClose(fh, NULL, NULL, 0);
    if (bytesRead < 0)
    {
        Display_printf(display, 0, 0,
            "readTypeField: read failed on %s\n\r", filename);
        return -1;
    }
    readBuf[bytesRead] = '\0';

    /* Extract "type" value with a direct string search -- avoids JSON
     * handle allocation that would exceed the library's simultaneous
     * handle limit when bootLCD preempts httpWorkerThread.           */
    const char *key = strstr(readBuf, "\"type\":\"");
    if (key == NULL)
    {
        Display_printf(display, 0, 0,
            "readTypeField: \"type\" key not found in %s\n\r", filename);
        return -1;
    }
    key += 8; /* skip past "type":" */
    const char *end = strchr(key, '"');
    if (end == NULL)
    {
        Display_printf(display, 0, 0,
            "readTypeField: unterminated type value in %s\n\r", filename);
        return -1;
    }
    uint16_t len = (uint16_t)(end - key);
    if (len >= bufLen) len = bufLen - 1;
    memcpy(outBuf, key, len);
    outBuf[len] = '\0';

    Display_printf(display, 0, 0,
        "readTypeField: %s -> type = \"%s\"\n\r", filename, outBuf);
    return 0;
}

/* ------------------------------------------------------------------ */
/* httpConnect()  -- shared setup: create handle, set headers, connect */
/* ------------------------------------------------------------------ */
static HTTPClient_Handle httpConnect(void)
{
    Display_printf(display, 0, 0, "[ENTER] httpConnect\n\r");
    int16_t statusCode = 0;
    int16_t ret;

    HTTPClient_Handle client = HTTPClient_create(&statusCode, 0);
    if (client == NULL)
    {
        Display_printf(display, 0, 0,
            "httpConnect: create failed, status=%d\n\r", statusCode);
        return NULL;
    }

    ret = HTTPClient_setHeader(client, HTTPClient_HFIELD_REQ_USER_AGENT,
                               USER_AGENT, strlen(USER_AGENT) + 1,
                               HTTPClient_HFIELD_PERSISTENT);
    if (ret < 0)
    {
        Display_printf(display, 0, 0,
            "httpConnect: set User-Agent failed, ret=%d\n\r", ret);
        HTTPClient_destroy(client);
        return NULL;
    }

    ret = HTTPClient_setHeader(client, HTTPClient_HFIELD_REQ_CONTENT_TYPE,
                               "application/json",
                               strlen("application/json") + 1,
                               HTTPClient_HFIELD_PERSISTENT);
    if (ret < 0)
    {
        Display_printf(display, 0, 0,
            "httpConnect: set Content-Type failed, ret=%d\n\r", ret);
        HTTPClient_destroy(client);
        return NULL;
    }

    /* TLS security parameters -- root CA only (no client cert/key).
     * The NWP verifies the server certificate against ca.pem which must
     * be programmed onto the SimpleLink FS via UniFlash before running. */
    HTTPClient_extSecParams secParams;
    memset(&secParams, 0, sizeof(secParams));
    secParams.rootCa     = ROOT_CA_FILE;
    secParams.clientCert = NULL;
    secParams.privateKey = NULL;

    /* Retry loop: -2006 (EALREADY) means the NWP hasn't finished releasing
     * the previous socket yet — its socket reclaim is asynchronous.  A
     * short delay is enough for the NWP to catch up.  No disconnect is
     * called on a failed attempt because no socket was opened.          */
#define HTTP_MAX_CONNECT_ATTEMPTS 3
#define HTTP_CONNECT_RETRY_S     4   /* 4 s between attempts      */
    {
        int attempt;
        for (attempt = 0; attempt < HTTP_MAX_CONNECT_ATTEMPTS; attempt++)
        {
            ret = HTTPClient_connect(client, HOSTNAME, &secParams, 0);
            if (ret >= 0)
                break;
            else if (ret == -111)
            {
                Display_printf(display, 0, 0, "httpConnect refused: aborting retry loop\n\r");
                break;
            }

            Display_printf(display, 0, 0,
                "httpConnect: attempt %d/%d failed, ret=%d\n\r",
                attempt + 1, HTTP_MAX_CONNECT_ATTEMPTS, ret);

            if (attempt + 1 < HTTP_MAX_CONNECT_ATTEMPTS)
                sleep(HTTP_CONNECT_RETRY_S);
        }
    }
    g_lastHttpConnectRet = ret;

    if (ret < 0)
    {
        HTTPClient_destroy(client);
        return NULL;
    }

    Display_printf(display, 0, 0, "httpConnect: connected to %s\n\r", HOSTNAME);
    return client;
}

/* ------------------------------------------------------------------ */
/* httpPost()
 *
 *  1. Reads the "type" field from `filename` on the SimpleLink FS.
 *  2. POSTs the file's JSON contents to HOSTNAME/REQUEST_URL.
 *  3. Branches on type:
 *
 *     type == "UpdateMCU"   -- accumulates the full response body and
 *                              overwrites `filename` on the FS with it.
 *
 *     any other type        -- drains and discards the response body
 *                              (server is receiving a report; FS file
 *                              is left unchanged).
 *
 *  Returns  0   success
 *          -1   network / HTTP / parse error
 *          -2   "UpdateMCU" mode: empty response body, file unchanged
 * ------------------------------------------------------------------ */
int httpPost(const char *filename)
{
    Display_printf(display, 0, 0, "[ENTER] httpPost\n\r");
    /* 1. Read "type" field to determine mode */
    char typeStr[32] = {0};
    if (readTypeField(filename, typeStr, sizeof(typeStr)) < 0)
    {
        Display_printf(display, 0, 0,
            "httpPost: cannot read type from %s, defaulting to updateWebApp\n\r",
            filename);
        /* safe default: push-only, never overwrite */
    }

    int updateMCU = (strcmp(typeStr, TYPE_UPDATE_MCU) == 0);
    Display_printf(display, 0, 0,
        "httpPost: file=%s  type=\"%s\"  mode=%s\n\r",
        filename, typeStr,
        updateMCU ? "updateMCU" : "updateWebApp");

    /* 2. Read payload from FS */
    char *payload = grabJson((char *)filename);
    if (payload == NULL)
    {
        Display_printf(display, 0, 0,
            "httpPost: grabJson(%s) failed\n\r", filename);
        return -1;
    }

    uint16_t payloadLen = getJsonStrLen(filename);
    if (payloadLen == 0)
    {
        Display_printf(display, 0, 0,
            "httpPost: %s is empty\n\r", filename);
        return -1;
    }

    /* 3. Connect */
    HTTPClient_Handle client = httpConnect();
    if (client == NULL)
    {
        updateWifiIcon(0);   /* server unreachable -- show no-wifi icon */
        return -1;
    }

    /* 4. Send POST */
    int16_t ret = HTTPClient_sendRequest(client, HTTP_METHOD_POST,
                                         REQUEST_URL, payload, payloadLen, 0);
    if (ret < 0)
    {
        Display_printf(display, 0, 0,
            "httpPost: sendRequest failed, ret=%d\n\r", ret);
        HTTPClient_disconnect(client);
        HTTPClient_destroy(client);
        updateWifiIcon(0);
        return -1;
    }

    updateWifiIcon(1);
    Display_printf(display, 0, 0, "httpPost: POST sent\n\r");

    /* 5a. updateWebApp -- drain and discard response body */
    if (!updateMCU)
    {
        bool     moreData = false;
        char     drain[HTTP_RECV_BUF];
        uint16_t total = 0;

        do {
            ret = HTTPClient_readResponseBody(client, drain,
                                             sizeof(drain), &moreData);
            if (ret > 0) total += (uint16_t)ret;
        } while (moreData);

        HTTPClient_disconnect(client);
        HTTPClient_destroy(client);
        Display_printf(display, 0, 0,
            "httpPost: updateWebApp done, drained %u bytes\n\r", total);
        return 0;
    }

    /* 5b. updateMCU -- accumulate full response body */
    memset(httpRespBuf, 0, sizeof(httpRespBuf));
    uint16_t totalLen = 0;
    bool     moreData = false;
    char     chunk[HTTP_RECV_BUF];

    do {
        ret = HTTPClient_readResponseBody(client, chunk,
                                          sizeof(chunk), &moreData);
        if (ret > 0)
        {
            uint16_t space = (uint16_t)(sizeof(httpRespBuf) - 1) - totalLen;
            uint16_t copy  = ((uint16_t)ret < space) ? (uint16_t)ret : space;
            memcpy(httpRespBuf + totalLen, chunk, copy);
            totalLen += copy;
        }
    } while (moreData);

    HTTPClient_disconnect(client);
    HTTPClient_destroy(client);

    Display_printf(display, 0, 0,
        "httpPost: received %u bytes\n\r", totalLen);

    if (totalLen == 0)
    {
        Display_printf(display, 0, 0,
            "httpPost: empty response, %s unchanged\n\r", filename);
        return -2;
    }

    httpRespBuf[totalLen] = '\0';
    Display_printf(display, 0, 0,
        "httpPost: response = %s\n\r", httpRespBuf);

    /* 5c. Validate response structure using string search -- avoids JSON
     * handle allocation that would exceed the library's simultaneous
     * handle limit when bootLCD preempts httpWorkerThread.
     * A valid UpdateMCU response must open with '{', contain "type":,
     * and contain a closing '}'.                                        */
    {
        static uint8_t parseFailCount = 0;
        const char *p = httpRespBuf;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        int16_t vRet = (*p == '{' &&
                        strstr(httpRespBuf, "\"type\":") != NULL &&
                        strchr(httpRespBuf, '}')         != NULL) ? 0 : -1;

        if (vRet < 0)
        {
            parseFailCount++;
            if (parseFailCount < 2)
            {
                Display_printf(display, 0, 0,
                    "httpPost: response corrupt (attempt %d), retrying\n\r",
                    (int)parseFailCount);
                return httpPost(filename);   /* silent retry */
            }
            else
            {
                Display_printf(display, 0, 0,
                    "httpPost: response corrupt twice -- fatal\n\r");
                parseFailCount = 0;   /* reset for next session */
                logError(FatalParsing, filename, -1);
                return -1;
            }
        }
        parseFailCount = 0;   /* successful validation -- reset counter */
    }

    /* 6. Overwrite FS file with server response */
    int32_t fh = sl_FsOpen((unsigned char *)filename,
                            SL_FS_CREATE | SL_FS_OVERWRITE |
                            SL_FS_CREATE_MAX_SIZE(JSON_SIZE), NULL);
    if (fh < 0)
    {
        Display_printf(display, 0, 0,
            "httpPost: sl_FsOpen(%s) failed, ret=%d\n\r",
            filename, (int)fh);
        return -1;
    }

    int32_t written = sl_FsWrite(fh, 0,
                                  (unsigned char *)httpRespBuf, totalLen + 1);
    sl_FsClose(fh, NULL, NULL, 0);

    if (written < 0)
    {
        Display_printf(display, 0, 0,
            "httpPost: sl_FsWrite failed, ret=%d\n\r", (int)written);
        return -1;
    }

    Display_printf(display, 0, 0,
        "httpPost: %s updated (%d bytes written)\n\r", filename, (int)written);
    return 0;
}
/* ------------------------------------------------------------------ */
/* httpPostGetResponse()
 *
 *  POSTs `filename` to the server and copies the response body into
 *  `respBuf` (max `respBufLen` bytes, null-terminated).
 *  The FS file is never modified regardless of the response.
 *
 *  Returns  0   success, respBuf contains the server response.
 *          -1   network / HTTP error, respBuf is empty.
 *          -2   POST succeeded but server sent an empty body.
 * ------------------------------------------------------------------ */
int httpPostGetResponse(const char *filename, char *respBuf, uint16_t respBufLen)
{
    Display_printf(display, 0, 0, "[ENTER] httpPostGetResponse\n\r");
    if (respBufLen == 0)
    {
        Display_printf(display, 0, 0,
            "httpPostGetResponse: respBufLen is 0, aborting\n\r");
        return -1;
    }
    memset(respBuf, 0, respBufLen);

    /* 1. Read payload from FS */
    char *payload = grabJson((char *)filename);
    if (payload == NULL) return -1;

    uint16_t payloadLen = getJsonStrLen(filename);
    if (payloadLen == 0) return -1;

    /* 2. Connect */
    HTTPClient_Handle client = httpConnect();
    if (client == NULL) return -1;

    /* 3. Send POST */
    int16_t ret = HTTPClient_sendRequest(client, HTTP_METHOD_POST,
                                         REQUEST_URL, payload, payloadLen, 0);
    if (ret < 0)
    {
        HTTPClient_disconnect(client);
        HTTPClient_destroy(client);
        /* Guard: only update the LCD icon if initLCD_hardware() has already
         * opened the SPI handle.  During jsonThread() init the handle is NULL
         * and calling drawNoWifiIcon() would dereference it -> HardFault.    */
        if (spi != NULL) updateWifiIcon(0);
        return -1;
    }

    if (spi != NULL) updateWifiIcon(1);

    /* 4. Accumulate response into caller's buffer */
    uint16_t totalLen = 0;
    bool     moreData = false;
    char     chunk[HTTP_RECV_BUF];

    do {
        ret = HTTPClient_readResponseBody(client, chunk,
                                          sizeof(chunk), &moreData);
        if (ret > 0)
        {
            uint16_t space = (respBufLen - 1) - totalLen;
            uint16_t copy  = ((uint16_t)ret < space) ? (uint16_t)ret : space;
            memcpy(respBuf + totalLen, chunk, copy);
            totalLen += copy;
        }
    } while (moreData);

    HTTPClient_disconnect(client);
    HTTPClient_destroy(client);

    if (totalLen == 0) return -2;

    respBuf[totalLen] = '\0';
    Display_printf(display, 0, 0,
        "httpPostGetResponse(%s): %u bytes -- %s\n\r",
        filename, totalLen, respBuf);
    return 0;
}
