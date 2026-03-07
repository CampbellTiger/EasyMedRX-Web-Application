#include "string.h"
#include <ti/display/Display.h>
#include <ti/net/http/httpclient.h>
#include "semaphore.h"
#include <stdio.h>
#include <ti/utils/json/json.h>

#define HOSTNAME              "reqbin.com"
#define REQUEST_URI           "/echo/post/json"
#define USER_AGENT            "HTTPClient (ARM; TI-RTOS)"
#define HTTP_MIN_RECV         (256)

extern Display_Handle display;
extern sem_t ipEventSyncObj;
extern void printError(char *errString, int code);

void* httpsTask(void* pvParameters)                                     /*GET*/
{
    bool moreDataFlag = false;
    char data[HTTP_MIN_RECV];
    int16_t ret = 0;                                                    /*Stores status code*/
    int16_t len = 0;

    Display_printf(display, 0, 0, "Sending a HTTP GET request to '%s'\n",
                   HOSTNAME);

    HTTPClient_Handle httpsClientHandle;
    int16_t statusCode;
    httpsClientHandle = HTTPClient_create(&statusCode,0);

    ret =
        HTTPClient_setHeader(httpsClientHandle,
                             HTTPClient_HFIELD_REQ_USER_AGENT,
                             USER_AGENT,strlen(USER_AGENT)+1,
                             HTTPClient_HFIELD_PERSISTENT);


    ret = HTTPClient_connect(httpsClientHandle, HOSTNAME, 0, 0);
    ret = HTTPClient_sendRequest(httpsClientHandle, HTTP_METHOD_GET,REQUEST_URI, NULL, 0, 0);

    len = 0;
    do
    {
        ret = HTTPClient_readResponseBody(httpsClientHandle, data, sizeof(data),
                                          &moreDataFlag);

        Display_printf(display, 0, 0, "%.*s \r\n",ret,data);
        len += ret;
    }
    while(moreDataFlag);

    Display_printf(display, 0, 0, "Received %d bytes of payload\n", len);

    ret = HTTPClient_disconnect(httpsClientHandle);

    HTTPClient_destroy(httpsClientHandle); /*destroy and free up the handler*/
    return(0);
}
void* httpsPut(void* pvParameters) /*GET*/
{
    HTTPClient_Handle httpsClientHandle;                    /*declare client handle*/
    int16_t statusCode;                                     /*declare status code to store status*/
    int16_t ret = 0;                                        /*return code*/
    int16_t len = 0;                                        /*length of data*/
    httpsClientHandle = HTTPClient_create(&statusCode,0);

    ret =
            HTTPClient_setHeader(httpsClientHandle,
                                 HTTPClient_HFIELD_REQ_USER_AGENT,
                                 USER_AGENT,
                                 strlen(USER_AGENT)+1,
                                 HTTPClient_HFIELD_PERSISTENT);
    ret =
           HTTPClient_sendRequest(httpsClientHandle,           /*handle*/
                                  HTTP_METHOD_GET, /*Action*/
                                  REQUEST_URI,     /*URI path*/
                                  NULL,            /*json goes here*/
                                  0,               /*length of json*/
                                  0);

    return(0);
}

void* httpsPost(void* pvParameters)
{
    bool moreDataFlag = false;
    char data[HTTP_MIN_RECV];
    int16_t ret = 0;
    int16_t len = 0;

    Display_printf(display, 0, 0, "Sending a HTTP POST request to '%s'\n", HOSTNAME);

    HTTPClient_Handle httpsClientHandle;
    int16_t statusCode;

    Display_printf(display, 0, 0, "exSecParams set\n");

    /*Create Http handle*/
    httpsClientHandle = HTTPClient_create(&statusCode, 0);
    if(httpsClientHandle == NULL) {
        Display_printf(display, 0, 0, "Failed to create HTTP client, status=%d\n", statusCode);
        return NULL;
    }
    Display_printf(display, 0, 0, "Client handle created, status=%d\n", statusCode);

    /*Set User-Agent header*/
    ret = HTTPClient_setHeader(httpsClientHandle,
                               HTTPClient_HFIELD_REQ_USER_AGENT,
                               USER_AGENT,
                               strlen(USER_AGENT)+1,
                               HTTPClient_HFIELD_PERSISTENT);
    if(ret < 0) {
        Display_printf(display, 0, 0, "User-Agent Header Failure, ret=%d\n", ret);
        HTTPClient_destroy(httpsClientHandle);
        return NULL;
    }
    Display_printf(display, 0, 0, "User-Agent Header created =%d\n", statusCode);

    /*Declare Content-Type as JSON*/
    ret = HTTPClient_setHeader(httpsClientHandle,
                               HTTPClient_HFIELD_REQ_CONTENT_TYPE,
                               "application/json",
                               strlen("application/json") + 1,
                               HTTPClient_HFIELD_PERSISTENT);
    if(ret < 0) {
        Display_printf(display, 0, 0, "Content type failure, ret = %d\n", ret);
        HTTPClient_destroy(httpsClientHandle);
        return NULL;
    }

    /*Connect to server*/
    ret = HTTPClient_connect(httpsClientHandle, HOSTNAME, 0, 0);
    if(ret < 0) {
        Display_printf(display, 0, 0, "Failed to connect, ret = %d\n", ret);
        HTTPClient_destroy(httpsClientHandle);
        return NULL;
    }
    Display_printf(display, 0, 0, "connected to %s, ret = %d\n", HOSTNAME, ret);

    /* Send POST request with JSON payload */
    ret = HTTPClient_sendRequest(httpsClientHandle,
                                 HTTP_METHOD_POST,
                                 REQUEST_URI,
                                 "Test",
                                 strlen("Test"),
                                 0);

    Display_printf(display, 0, 0, "Send request returned: %d\n", ret);

    if(ret < 0) {
        Display_printf(display, 0, 0, "Send request failure, ret = %d\n", ret);
        HTTPClient_disconnect(httpsClientHandle);
        HTTPClient_destroy(httpsClientHandle);
        return NULL;
    }

    Display_printf(display, 0, 0, "Send request success to %s, ret = %d\n", HOSTNAME ,ret);

    /* Read response body */
    len = 0;
    int numLoop = 0;
    do {
        numLoop+=1;
        ret = HTTPClient_readResponseBody(httpsClientHandle, data, sizeof(data), &moreDataFlag);
        Display_printf(display, 0, 0, "Loop %d, Return code: %d, len = %d \r\n",numLoop, ret, len);
        if(ret > 0) {
            Display_printf(display, 0, 0, "%.*s\r\n", ret, data);
            len += ret;
        }
    } while(moreDataFlag);

    Display_printf(display, 0, 0, "Received %d bytes of payload\n", len);

    /* Disconnect and Destroy handles */
    HTTPClient_disconnect(httpsClientHandle);
    HTTPClient_destroy(httpsClientHandle);

    return 0;
}



