/*
 *  ======== httpget.c ========
 *  HTTP Client GET example application
 */

/* BSD support */
#include "string.h"
#include <ti/display/Display.h>
#include <ti/net/http/httpclient.h>
#include "semaphore.h"

#define HOSTNAME              "https://example.com/"
#define REQUEST_URI           "/"
#define USER_AGENT            "HTTPClient (ARM; TI-RTOS)"
#define HTTP_MIN_RECV         (256)

extern Display_Handle display;
extern sem_t ipEventSyncObj;
extern void printError(char *errString,
                       int code);

/*
 *  ======== httpsTask ========
 *  Makes a HTTP GET request
 */
void* httpsTask(void* pvParameters)
{
    bool moreDataFlag = false;
    char data[HTTP_MIN_RECV];
    int16_t ret = 0;
    int16_t len = 0;

    Display_printf(display, 0, 0, "Sending a HTTP GET request to '%s'\n",
                   HOSTNAME);

    HTTPClient_Handle httpsClientHandle;
    int16_t statusCode;
    httpsClientHandle = HTTPClient_create(&statusCode,0);
    if(statusCode < 0)
    {
        printError("httpsTask: creation of http client handle failed",
                   statusCode);
    }

    ret =
        HTTPClient_setHeader(httpsClientHandle,
                             HTTPClient_HFIELD_REQ_USER_AGENT,
                             USER_AGENT,strlen(USER_AGENT)+1,
                             HTTPClient_HFIELD_PERSISTENT);
    if(ret < 0)
    {
        printError("httpsTask: setting request header failed", ret);
    }

    ret = HTTPClient_connect(httpsClientHandle,HOSTNAME,0,0);
    if(ret < 0)
    {
        printError("httpsTask: connect failed", ret);
    }
    ret =
        HTTPClient_sendRequest(httpsClientHandle,HTTP_METHOD_GET,REQUEST_URI,
                               NULL,0,
                               0);
    if(ret < 0)
    {
        printError("httpsTask: send failed", ret);
    }

    if(ret != HTTP_SC_OK)
    {
        printError("httpsTask: cannot get status", ret);
    }

    Display_printf(display, 0, 0, "HTTP Response Status Code: %d\n", ret);

    len = 0;
    do
    {
        ret = HTTPClient_readResponseBody(httpsClientHandle, data, sizeof(data),
                                          &moreDataFlag);
        if(ret < 0)
        {
            printError("httpsTask: response body processing failed", ret);
        }
        Display_printf(display, 0, 0, "%.*s \r\n",ret,data);
        len += ret;
    }
    while(moreDataFlag);

    Display_printf(display, 0, 0, "Received %d bytes of payload\n", len);

    ret = HTTPClient_disconnect(httpsClientHandle);
    if(ret < 0)
    {
        printError("httpsTask: disconnect failed", ret);
    }

    HTTPClient_destroy(httpsClientHandle);
    return(0);
}
