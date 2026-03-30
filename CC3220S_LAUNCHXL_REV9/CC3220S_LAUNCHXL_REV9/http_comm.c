#include "string.h"
#include <ti/display/Display.h>
#include <ti/net/http/httpclient.h>
#include "semaphore.h"
#include <stdio.h>
#include <ti/utils/json/json.h>
#include "json_make.h"
#include "platform.h"

#define HOSTNAME              "http://10.102.253.90:8000"
#define REQUEST_URL           "/api/device/push"
#define USER_AGENT            "HTTPClient (ARM; TI-RTOS)"
#define HTTP_MIN_RECV         (256)

extern Display_Handle display;
extern sem_t ipEventSyncObj;
extern void printError(char *errString, int code);

void* httpPost(char *payload, uint16_t payloadLen)
{
    bool moreDataFlag = false;
    char data[HTTP_MIN_RECV];
    int16_t ret = 0;
    int16_t len = 0;

    DisplayBanner(APPLICATION_NAME2);

    Display_printf(display, 0, 0, "Sending a HTTP POST request to '%s'\n", HOSTNAME);

    HTTPClient_Handle httpClientHandle;
    int16_t statusCode;

    /*Create Http handle*/
    httpClientHandle = HTTPClient_create(&statusCode, 0);
    if(httpClientHandle == NULL) {
        Display_printf(display, 0, 0, "Failed to create HTTP client, status=%d\n", statusCode);
        return NULL;
    }
    Display_printf(display, 0, 0, "Client handle created, status=%d\n", statusCode);

    /*Set User-Agent header*/
    ret = HTTPClient_setHeader(httpClientHandle,
                               HTTPClient_HFIELD_REQ_USER_AGENT,
                               USER_AGENT,
                               strlen(USER_AGENT)+1,
                               HTTPClient_HFIELD_PERSISTENT);
    if(ret < 0) {
        Display_printf(display, 0, 0, "User-Agent Header Failure, ret=%d\n", ret);
        HTTPClient_destroy(httpClientHandle);
        return NULL;
    }
    Display_printf(display, 0, 0, "User-Agent Header created =%d\n", statusCode);

    /*Declare Content-Type as JSON*/
    ret = HTTPClient_setHeader(httpClientHandle,
                               HTTPClient_HFIELD_REQ_CONTENT_TYPE,
                               "application/json",
                               strlen("application/json") + 1,
                               HTTPClient_HFIELD_PERSISTENT);
    if(ret < 0) {
        Display_printf(display, 0, 0, "Content type failure, ret = %d\n", ret);
        HTTPClient_destroy(httpClientHandle);
        return NULL;
    }

    /*Connect to server*/
    ret = HTTPClient_connect(httpClientHandle, HOSTNAME, 0, 0);
    if(ret < 0) {
        Display_printf(display, 0, 0, "Failed to connect, ret = %d\n", ret);
        HTTPClient_destroy(httpClientHandle);
        return NULL;
    }
    Display_printf(display, 0, 0, "connected to %s, ret = %d\n", HOSTNAME, ret);

    /* Send POST request with JSON payload */
    ret = HTTPClient_sendRequest(httpClientHandle,
                                 HTTP_METHOD_POST,
                                 REQUEST_URL,
                                 payload,
                                 payloadLen,
                                 0);

    Display_printf(display, 0, 0, "Send request returned: %d\n", ret);

    if(ret < 0) {
        Display_printf(display, 0, 0, "Send request failure, ret = %d\n", ret);
        HTTPClient_disconnect(httpClientHandle);
        HTTPClient_destroy(httpClientHandle);
        return NULL;
    }

    Display_printf(display, 0, 0, "Send request success to %s, ret = %d\n", HOSTNAME ,ret);

    /* Read response body */
    len = 0;
    int numLoop = 0;
    do {
        numLoop+=1;
        ret = HTTPClient_readResponseBody(httpClientHandle, data, sizeof(data), &moreDataFlag);
        Display_printf(display, 0, 0, "Loop %d, Return code: %d, len = %d \r\n",numLoop, ret, len);
        if(ret > 0) {
            Display_printf(display, 0, 0, "%.*s\r\n", ret, data);
            len += ret;
        }
    } while(moreDataFlag);

    Display_printf(display, 0, 0, "Received %d bytes of payload\n", len);

    /* Disconnect and Destroy handles */
    HTTPClient_disconnect(httpClientHandle);
    HTTPClient_destroy(httpClientHandle);

    return 0;
}



