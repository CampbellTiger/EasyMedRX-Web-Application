/*
 *  ======== platform.c ========
 */
#include <ti/drivers/net/wifi/simplelink.h>
#include <ti/drivers/net/wifi/slnetifwifi.h>

#include <ti/display/Display.h>
#include <ti/drivers/SPI.h>

#include "ti_drivers_config.h"
#include "pthread.h"
#include "semaphore.h"
#include <string.h>

#define APPLICATION_NAME1                     "HTTP GET"
#define APPLICATION_NAME2                     "HTTPS GET"
#define DEVICE_ERROR                          ("Device error, please refer \"DEVICE ERRORS CODES\" section in errors.h")
#define WLAN_ERROR                            ("WLAN error, please refer \"WLAN ERRORS CODES\" section in errors.h")
#define SL_STOP_TIMEOUT                       (200)
#define SPAWN_TASK_PRIORITY                   (9)
#define SPAWN_STACK_SIZE                      (4096)
#define TASK_STACK_SIZE                       (2048)
#define SLNET_IF_WIFI_PRIO                    (5)
#define SLNET_IF_WIFI_NAME                    "CC32xx"
/* AP SSID */
#define SSID_NAME                             "SK_2.4G"
#define SECURITY_TYPE                         SL_WLAN_SEC_TYPE_WPA_WPA2
#define SECURITY_KEY                          "breadcrumbs!"

#define EAP_SSID_NAME                         "TAMU_WiFi"
#define EAP_USERNAME                          "graham_anderson@tamu.edu"
#define EAP_PASSWORD                          "CENSORED"

/* Index of the CA certificate*/
#define EAP_CA_CERT_INDEX                     0

pthread_t httpsThread = (pthread_t)NULL;                                            /*http vs https*/
pthread_t spawn_thread = (pthread_t)NULL;

int32_t mode;
Display_Handle display;

extern void* httpsTask(void* pvParameters);
extern int32_t ti_net_SlNet_initConfig();

/*
 *  ======== printError ========
 */
void printError(char *errString,
                int code)
{
    Display_printf(display, 0, 0, "Error! code = %d, Description = %s\n", code,
                   errString);
    while(1)
    {
        ;
    }
}

/*!
    \brief          SimpleLinkNetAppEventHandler

    This handler gets called whenever a Netapp event is reported
    by the host driver / NWP. Here user can implement he's own logic
    for any of these events. This handler is used by 'network_terminal'
    application to show case the following scenarios:

    1. Handling IPv4 / IPv6 IP address acquisition.
    2. Handling IPv4 / IPv6 IP address Dropping.

    \param          pNetAppEvent     -   pointer to Netapp event data.

    \return         void

    \note           For more information, please refer to: user.h in the porting
                    folder of the host driver and the  CC31xx/CC32xx
                    NWP programmer's
                    guide (SWRU455) section 5.7

 */
void SimpleLinkNetAppEventHandler(SlNetAppEvent_t *pNetAppEvent)
{
    int32_t             status = 0;
    pthread_attr_t      pAttrs;
    struct sched_param  priParam;

    if(pNetAppEvent == NULL)
    {
        return;
    }

    switch(pNetAppEvent->Id)
    {
    case SL_NETAPP_EVENT_IPV4_ACQUIRED:
    case SL_NETAPP_EVENT_IPV6_ACQUIRED:
        /* Initialize SlNetSock layer with CC3x20 interface                   */
        status = ti_net_SlNet_initConfig();
        if(0 != status)
        {
            Display_printf(display, 0, 0, "Failed to initialize SlNetSock\n\r");
        }

        if(mode != ROLE_AP)
        {
            Display_printf(display, 0, 0,"[NETAPP EVENT] IP Acquired: IP=%d.%d.%d.%d , "
                        "Gateway=%d.%d.%d.%d\n\r",
                        SL_IPV4_BYTE(pNetAppEvent->Data.IpAcquiredV4.Ip,3),
                        SL_IPV4_BYTE(pNetAppEvent->Data.IpAcquiredV4.Ip,2),
                        SL_IPV4_BYTE(pNetAppEvent->Data.IpAcquiredV4.Ip,1),
                        SL_IPV4_BYTE(pNetAppEvent->Data.IpAcquiredV4.Ip,0),
                        SL_IPV4_BYTE(pNetAppEvent->Data.IpAcquiredV4.Gateway,3),
                        SL_IPV4_BYTE(pNetAppEvent->Data.IpAcquiredV4.Gateway,2),
                        SL_IPV4_BYTE(pNetAppEvent->Data.IpAcquiredV4.Gateway,1),
                        SL_IPV4_BYTE(pNetAppEvent->Data.IpAcquiredV4.Gateway,0));

            pthread_attr_init(&pAttrs);
            priParam.sched_priority = 1;
            status = pthread_attr_setschedparam(&pAttrs, &priParam);
            status |= pthread_attr_setstacksize(&pAttrs, TASK_STACK_SIZE);

            status = pthread_create(&httpsThread, &pAttrs, httpsTask, NULL);                    /*http vs https*/
            if(status)
            {
                printError("Task create failed", status);
            }
        }
        break;
    default:
        break;
    }
}

/*!
    \brief          SimpleLinkFatalErrorEventHandler

    This handler gets called whenever a socket event is reported
    by the NWP / Host driver. After this routine is called, the user's
    application must restart the device in order to recover.

    \param          slFatalErrorEvent    -   pointer to fatal error event.

    \return         void

    \note           For more information, please refer to: user.h in the porting
                    folder of the host driver and the  CC31xx/CC32xx NWP
                    programmer's
                    guide (SWRU455) section 17.9.

 */
void SimpleLinkFatalErrorEventHandler(SlDeviceFatal_t *slFatalErrorEvent)
{
    /* Unused in this application */
}

/*!
    \brief          SimpleLinkNetAppRequestMemFreeEventHandler

    This handler gets called whenever the NWP is done handling with
    the buffer used in a NetApp request. This allows the use of
    dynamic memory with these requests.

    \param         pNetAppRequest     -   Pointer to NetApp request structure.

    \param         pNetAppResponse    -   Pointer to NetApp request Response.

    \note          For more information, please refer to: user.h in the porting
                   folder of the host driver and the  CC31xx/CC32xx NWP
                   programmer's
                   guide (SWRU455) section 17.9.

    \return        void

 */
void SimpleLinkNetAppRequestMemFreeEventHandler(uint8_t *buffer)
{
    /* Unused in this application */
}

/*!
    \brief         SimpleLinkNetAppRequestEventHandler

    This handler gets called whenever a NetApp event is reported
    by the NWP / Host driver. User can write he's logic to handle
    the event here.

    \param         pNetAppRequest     -   Pointer to NetApp request structure.

    \param         pNetAppResponse    -   Pointer to NetApp request Response.

    \note          For more information, please refer to: user.h in the porting
                   folder of the host driver and the  CC31xx/CC32xx NWP
                   programmer's
                   guide (SWRU455) section 17.9.

    \return         void

 */
void SimpleLinkNetAppRequestEventHandler(SlNetAppRequest_t *pNetAppRequest,
                                         SlNetAppResponse_t *pNetAppResponse)
{
    /* Unused in this application */
}

/*!
    \brief          SimpleLinkHttpServerEventHandler

    This handler gets called whenever a HTTP event is reported
    by the NWP internal HTTP server.

    \param          pHttpEvent       -   pointer to http event data.

    \param          pHttpEvent       -   pointer to http response.

    \return         void

    \note          For more information, please refer to: user.h in the porting
                   folder of the host driver and the  CC31xx/CC32xx NWP
                   programmer's
                   guide (SWRU455) chapter 9.

 */
void SimpleLinkHttpServerEventHandler(
    SlNetAppHttpServerEvent_t *pHttpEvent,
    SlNetAppHttpServerResponse_t *
    pHttpResponse)
{
    /* Unused in this application */
}

/*!
    \brief          SimpleLinkWlanEventHandler

    This handler gets called whenever a WLAN event is reported
    by the host driver / NWP. Here user can implement he's own logic
    for any of these events. This handler is used by 'network_terminal'
    application to show case the following scenarios:

    1. Handling connection / Disconnection.
    2. Handling Addition of station / removal.
    3. RX filter match handler.
    4. P2P connection establishment.

    \param          pWlanEvent       -   pointer to Wlan event data.

    \return         void

    \note          For more information, please refer to: user.h in the porting
                   folder of the host driver and the  CC31xx/CC32xx
                   NWP programmer's
                   guide (SWRU455) sections 4.3.4, 4.4.5 and 4.5.5.

    \sa             cmdWlanConnectCallback, cmdEnableFilterCallback, 
    cmdWlanDisconnectCallback,
                    cmdP2PModecallback.

 */
void SimpleLinkWlanEventHandler(SlWlanEvent_t *pWlanEvent)
{
    if (pWlanEvent == NULL) return;

    switch(pWlanEvent->Id)
    {
        case SL_WLAN_EVENT_CONNECT:
            Display_printf(display, 0, 0, "[WLAN EVENT] Connected to AP\n");
            break;

        case SL_WLAN_EVENT_DISCONNECT:
            Display_printf(display, 0, 0,
                "[WLAN EVENT] Disconnected from AP. Reason Code: %d\n",
                pWlanEvent->Data.Disconnect.ReasonCode);
            break;

        default:
            Display_printf(display, 0, 0, "[WLAN EVENT] Unknown event: %d\n", pWlanEvent->Id);
            break;
    }
}

void SimpleLinkGeneralEventHandler(SlDeviceEvent_t *pDevEvent)
{
    /* Unused in this application */
}

/*!
    \brief          SimpleLinkSockEventHandler

    This handler gets called whenever a socket event is reported
    by the NWP / Host driver.

    \param          SlSockEvent_t    -   pointer to socket event data.

    \return         void

    \note          For more information, please refer to: user.h in the porting
                   folder of the host driver and the  CC31xx/CC32xx NWP
                   programmer's
                   guide (SWRU455) section 7.6.
                   

 */
void SimpleLinkSockEventHandler(SlSockEvent_t *pSock)
{
    /* Unused in this application */
}

int Connect(void)
{
    SlWlanSecParams_t secParams = {0};
    int16_t ret = 0;
    secParams.Key = (signed char*)SECURITY_KEY;
    secParams.KeyLen = strlen(SECURITY_KEY);
    secParams.Type = SECURITY_TYPE;
    Display_printf(display, 0, 0, "Connecting to : %s.\r\n",SSID_NAME);             /*blocking request*/

    ret = sl_WlanConnect((signed char*)SSID_NAME, strlen(                           /*Send connection request*/
                             SSID_NAME), 0, &secParams, 0);

    if(ret != 0)
    {
        Display_printf(display, 0, 0, "Failed WPA/WPA2 request\n");
        return ret;
    }
    Display_printf(display, 0, 0, "Failed WPA/WPA2 request\n");
    return 0;
}

void EapConnect(void)
{
    SlWlanSecParams_t secParams = {0};
    SlWlanSecParamsExt_t secExtParams = {0};

    /* SSID */
    const char *ssid = EAP_SSID_NAME;

    /* Enterprise credentials */
    const char *username = EAP_USERNAME;
    const char *password = EAP_PASSWORD;                                            /* goes in secParams.Key */

    /* Set enterprise security type */
    secParams.Type = SL_WLAN_SEC_TYPE_WPA_ENT;
    secParams.Key = (signed char *)password;                                        /* EAP password */
    secParams.KeyLen = strlen(password);

    /* Extended params: identity */
    secExtParams.User = (signed char *)username;
    secExtParams.UserLen = strlen(username);
    secExtParams.AnonUser = (signed char *)"";
    secExtParams.AnonUserLen = 0;
    secExtParams.CertIndex  = 0;
    secExtParams.EapMethod = SL_WLAN_ENT_EAP_METHOD_PEAP0_MSCHAPv2;
    secExtParams.CertIndex = EAP_CA_CERT_INDEX;                                     /*Use CA certificate needed on TAMU_WiFi*/

    Display_printf(display, 0, 0,
                   "Connecting with EAP: SSID=%s, user=%s\r\n",
                   ssid, username);

    int16_t ret = sl_WlanConnect((signed char *)ssid,
                                 strlen(ssid),
                                 0,
                                 &secParams,
                                 &secExtParams);
    if (ret < 0)
    {
        printError("EAP connect failed", ret);
    }
}





static void DisplayBanner(char * AppName)
{
    Display_printf(display, 0, 0, "\n\n\n\r");
    Display_printf(display, 0, 0,
                   "\t\t *************************"
                   "************************\n\r");
    Display_printf(display, 0, 0, "\t\t            %s Application       \n\r",
                   AppName);
    Display_printf(display, 0, 0,
                   "\t\t **************************"
                   "***********************\n\r");
    Display_printf(display, 0, 0, "\n\n\n\r");
}


void mainThread(void *pvParameters)
{
    int32_t status = 0;                                                                             /*scheduling httpget*/
    pthread_attr_t httpsAttrs_spawn;                                                                    /*dynamically scheduled thread*/
    struct sched_param httpsParam;
	
    SPI_init();
    Display_init();
    display = Display_open(Display_Type_UART, NULL);
    if(display == NULL)
    {
        /* Failed to open display driver */
        while(1)
        {
            ;
        }
    }

    /* Print Application name */
    DisplayBanner(APPLICATION_NAME2);

    /* Start the SimpleLink Host */
    pthread_attr_init(&httpsAttrs_spawn);
    httpsParam.sched_priority = SPAWN_TASK_PRIORITY;
    status = pthread_attr_setschedparam(&httpsAttrs_spawn, &httpsParam);
    status |= pthread_attr_setstacksize(&httpsAttrs_spawn, SPAWN_STACK_SIZE);

    status = pthread_create(&spawn_thread, &httpsAttrs_spawn, sl_Task, NULL);
    if(status)
    {
        printError("Task create failed", status);
    }

    /* Turn NWP on - initialize the device*/
    mode = sl_Start(0, 0, 0);
    if (mode < 0)
    {
        Display_printf(display, 0, 0,"\n\r[line:%d, error code:%d] %s\n\r", __LINE__, mode, DEVICE_ERROR);
    }

    if(mode != ROLE_STA)
    {
        /* Set NWP role as STA */
        mode = sl_WlanSetMode(ROLE_STA);
        if (mode < 0)
        {
            Display_printf(display, 0, 0,"\n\r[line:%d, error code:%d] %s\n\r", __LINE__, mode, WLAN_ERROR);
        }

        /* For changes to take affect, restart the NWP */
        status = sl_Stop(SL_STOP_TIMEOUT);                                          /*Event handler return code 1*/
        if (status < 0)
        {
            Display_printf(display, 0, 0,"\n\r[line:%d, error code:%d] %s\n\r", __LINE__, status, DEVICE_ERROR);
        }

        mode = sl_Start(0, 0, 0);
        if (mode < 0)
        {
            Display_printf(display, 0, 0,"\n\r[line:%d, error code:%d] %s\n\r", __LINE__, mode, DEVICE_ERROR);
        }
    }

    if(mode != ROLE_STA)
    {
        printError("Failed to configure device to it's default state", mode);
    }
    EapConnect();                                                                          /*Eap for TAMU_Wifi*/
}
