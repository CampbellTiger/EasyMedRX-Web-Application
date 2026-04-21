/*
 *  ======== platform.c ========
 */
#include <ti/drivers/net/wifi/simplelink.h>
#include <ti/drivers/net/wifi/slnetifwifi.h>

#include <ti/display/Display.h>
#include <ti/drivers/SPI.h>
#include <stdint.h>

#include "ti_drivers_config.h"
#include "pthread.h"
#include <semaphore.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "timeKeep.h"

#define APPLICATION_NAME1                     "HTTP GET"
#define APPLICATION_NAME2                     "HTTP POST"
#define DEVICE_ERROR                          ("Device error, please refer \"DEVICE ERRORS CODES\" section in errors.h")
#define WLAN_ERROR                            ("WLAN error, please refer \"WLAN ERRORS CODES\" section in errors.h")
#define SL_STOP_TIMEOUT                       (200)
#define SPAWN_TASK_PRIORITY                   (9)
#define SPAWN_STACK_SIZE                      (4096)
#define TASK_STACK_SIZE                       (2048)
#define SLNET_IF_WIFI_PRIO                    (5)
#define SLNET_IF_WIFI_NAME                    "CC32xx"

/* Forward declarations */
int Connect(void);
int TAMUConnect(void);
static int reconnectTAMU(void);

#define RECONNECT_MAX_ATTEMPTS  3
#define RECONNECT_RETRY_DELAY_S 2   /* seconds between reconnect attempts */

/* LCD wifi icon — defined in LCD_Menu.c.  Guard with spi != NULL check because
 * the SPI handle is NULL until initLCD_hardware() runs; calling the icon draw
 * before that causes a HardFault.                                            */
extern void          updateWifiIcon(uint8_t connected);
extern SPI_Handle    spi;   /* NULL until configSPI() runs in initLCD_hardware */

/* AP SSID */
#define SSID_NAME                             "AMOGUS"
#define SECURITY_TYPE                          SL_WLAN_SEC_TYPE_WPA_WPA2    /*for both home and TAMU_IoT*/
#define SECURITY_KEY                          "impostersus"

#define TAMU_IoT_NAME                         "TAMU_IoT"

#define DEVICE_NAME                           "CC3220S_GA"

/*IF SL_NETCFG_HOSTNAME NOT DEFINED*/
#ifndef SL_NETCFG_HOSTNAME
#define SL_NETCFG_HOSTNAME 0x2003   /*0x2003 = hostname parameter ID*/
#endif

/*MAC ADDRESS*/
unsigned char g_staMac[6] = {0};
bool g_staMacValid = false;
int16_t g_lastHttpConnectRet = 0;

pthread_t httpThread = (pthread_t)NULL;                                            /*http*/
pthread_t spawn_thread = (pthread_t)NULL;

int32_t mode;
Display_Handle display;

extern int32_t ti_net_SlNet_initConfig();
extern sem_t sem;

/*
 *  ======== printError ========
 */
void printError(char *errString,
                int code)
{
    Display_printf(display, 0, 0, "[ENTER] printError\n\r");
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
    Display_printf(display, 0, 0, "[ENTER] SimpleLinkNetAppEventHandler\n\r");
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

            Display_printf(display, 0, 0, "Wifi succeeded\n\r");

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
    Display_printf(display, 0, 0, "[ENTER] SimpleLinkFatalErrorEventHandler\n\r");
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
    Display_printf(display, 0, 0, "[ENTER] SimpleLinkNetAppRequestMemFreeEventHandler\n\r");
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
    Display_printf(display, 0, 0, "[ENTER] SimpleLinkNetAppRequestEventHandler\n\r");
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
    Display_printf(display, 0, 0, "[ENTER] SimpleLinkHttpServerEventHandler\n\r");
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
    Display_printf(display, 0, 0, "[ENTER] SimpleLinkWlanEventHandler\n\r");
    if (pWlanEvent == NULL) return;

    switch(pWlanEvent->Id)
    {
        case SL_WLAN_EVENT_CONNECT:
            Display_printf(display, 0, 0, "[WLAN EVENT] Connected to AP\n");
            break;

        case SL_WLAN_EVENT_DISCONNECT:
        {
            uint16_t reason = pWlanEvent->Data.Disconnect.ReasonCode;
            Display_printf(display, 0, 0,
                "[WLAN EVENT] Disconnected from AP. Reason Code: %d -- reconnecting\n",
                reason);
            if (spi != NULL) updateWifiIcon(0);

            /* Retry the connect request up to RECONNECT_MAX_ATTEMPTS times.
             * reconnectTAMU() only calls sl_WlanConnect — no hostname or MAC
             * setup — so it is safe and fast to call from event handler context.
             * No sleep between attempts: sl_WlanConnect is non-blocking and
             * sleeping here would stall the SimpleLink task.                  */
            int ret = -1;
            int attempt;
            for (attempt = 0; attempt < RECONNECT_MAX_ATTEMPTS; attempt++)
            {
                ret = reconnectTAMU();
                if (ret == 0)
                {
                    Display_printf(display, 0, 0,
                        "[WLAN EVENT] Reconnect request accepted (attempt %d)\n",
                        attempt + 1);
                    break;
                }
                Display_printf(display, 0, 0,
                    "[WLAN EVENT] Reconnect attempt %d/%d failed, ret=%d\n",
                    attempt + 1, RECONNECT_MAX_ATTEMPTS, ret);
                if (attempt + 1 < RECONNECT_MAX_ATTEMPTS)
                    sleep(RECONNECT_RETRY_DELAY_S);
            }
            if (ret != 0)
                Display_printf(display, 0, 0,
                    "[WLAN EVENT] All reconnect attempts failed\n");
            break;
        }

        default:
            Display_printf(display, 0, 0, "[WLAN EVENT] Unknown event: %d\n", pWlanEvent->Id);
            break;
    }
}

void SimpleLinkGeneralEventHandler(SlDeviceEvent_t *pDevEvent)
{
    Display_printf(display, 0, 0, "[ENTER] SimpleLinkGeneralEventHandler\n\r");
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
    Display_printf(display, 0, 0, "[ENTER] SimpleLinkSockEventHandler\n\r");
    /* Unused in this application */
}

int Connect(void)
{
    Display_printf(display, 0, 0, "[ENTER] Connect\n\r");
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
    Display_printf(display, 0, 0, "Connect request sent successfully, ret = %d\n", ret);
    return 0;
}

int TAMUConnect(void)
{
    Display_printf(display, 0, 0, "[ENTER] TAMUConnect\n\r");
    unsigned short len = 6;
    unsigned short configOpt = 0;
    char macStr[24];
    int16_t ret;

    ret = sl_NetCfgSet(SL_NETCFG_HOSTNAME, 1, strlen(DEVICE_NAME), (unsigned char*)DEVICE_NAME);
    Display_printf(display, 0, 0, "return code set hostname: %d\r\n", ret);

    if (ret == 0 && len > 0)
    {
        Display_printf(display, 0, 0, "Hostname set to: %s\r\n", DEVICE_NAME);
    }
   else
   {
        Display_printf(display, 0, 0, "Set hostname failed return code: %d\r\n", ret);
   }

    /* Display STA MAC Address*/

    ret = sl_NetCfgGet(SL_NETCFG_MAC_ADDRESS_GET,&configOpt,&len,g_staMac);

   if (ret != 0 || len != 6)
   {
       Display_printf(display, 0, 0, "Fetch STA MAC Address Failed\r\n");
       return ret;
   }

        g_staMacValid = true;

        /* Format MAC safely (Display_printf cannot do %02X) */
        snprintf(macStr, sizeof(macStr),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 g_staMac[0], g_staMac[1], g_staMac[2],
                 g_staMac[3], g_staMac[4], g_staMac[5]);

        Display_printf(display, 0, 0,
            "STA MAC: %s\r\n", macStr);

    /*MAC Based Authorization only, No secParam needed*/
    Display_printf(display, 0, 0, "Connecting to : %s.\r\n",TAMU_IoT_NAME);             /*blocking request*/

    ret = sl_WlanConnect((signed char*)TAMU_IoT_NAME, strlen(                           /*Send connection request*/
            TAMU_IoT_NAME), 0, NULL, 0);

    if(ret != 0)
    {
        Display_printf(display, 0, 0, "Failed WPA/WPA2 request\n");
        return ret;
    }
    Display_printf(display, 0, 0, "Successfully sent WPA/WPA2 request Return: %d\n", ret);
    return 0;
}

/* -----------------------------------------------------------------------
 * reconnectTAMU()
 *
 * Lean reconnect — issues sl_WlanConnect to TAMU_IoT only.
 * Hostname and MAC are set once at boot by TAMUConnect() and never
 * need to be repeated.  Safe to call from event handler context.
 *
 * Returns 0 if the connect request was accepted by the NWP,
 * negative SimpleLink error code otherwise.
 * --------------------------------------------------------------------- */
static int reconnectTAMU(void)
{
    return sl_WlanConnect((signed char *)TAMU_IoT_NAME,
                          strlen(TAMU_IoT_NAME), 0, NULL, 0);
}

void DisplayBanner(char * AppName)
{
    Display_printf(display, 0, 0, "[ENTER] DisplayBanner\n\r");
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


void checkAndReconnect(void)
{
    if (g_lastHttpConnectRet != -111)
        return;

    int16_t discRet = sl_WlanDisconnect();
    if (discRet >= 0)
    {
        /* 0 = cleanly disconnected; positive = was not connected.
         * Either way the NWP state is known — safe to reconnect. */
        reconnectTAMU();
    }
    else
    {
        /* Negative — unexpected NWP error.
         * Do not attempt to connect on top of an unknown state. */
        Display_printf(display, 0, 0,
            "checkAndReconnect: sl_WlanDisconnect err=%d, skipping reconnect\n\r",
            (int)discRet);
    }
}

void mainThread(void *pvParameters)
{
    Display_printf(display, 0, 0, "[ENTER] mainThread\n\r");
    int32_t status = 0;                                                                             /*scheduling httpget*/
    pthread_attr_t httpAttrs_spawn;                                                                /*dynamically scheduled thread*/
    struct sched_param httpParam;

    display = Display_open(Display_Type_UART, NULL);
    if(display == NULL)
    {
        /* Failed to open display driver */
        while(1)
        {
            ;
        }
    }

    /* Start the SimpleLink Host */
    pthread_attr_init(&httpAttrs_spawn);
    httpParam.sched_priority = SPAWN_TASK_PRIORITY;
    status = pthread_attr_setschedparam(&httpAttrs_spawn, &httpParam);
    status |= pthread_attr_setstacksize(&httpAttrs_spawn, SPAWN_STACK_SIZE);

    status = pthread_create(&spawn_thread, &httpAttrs_spawn, sl_Task, NULL);
    if(status)
    {
        printError("Task create failed", status);
    }

    else{
        Display_printf(display, 0, 0,"Sl_Task Spawned Successfully\r\n");
    }

    /* Turn NWP on - initialize the device*/
    mode = sl_Start(0, 0, 0);
    if (mode < 0)
    {
        Display_printf(display, 0, 0,"\r[line:%d, error code:%d] %s\n\r", __LINE__, mode, DEVICE_ERROR);
    }
    Display_printf(display, 0, 0,"Sl_Start() mode = %d\r\n", mode);

    if(mode != ROLE_STA)
    {
        /* Set NWP role as STA */
        mode = sl_WlanSetMode(ROLE_STA);
        if (mode < 0)
        {
            Display_printf(display, 0, 0,"[line:%d, error code:%d] %s\n\r", __LINE__, mode, WLAN_ERROR);
        }

        /* For changes to take affect, restart the NWP */
        status = sl_Stop(SL_STOP_TIMEOUT);                                          /*Event handler return code 1*/
        if (status < 0)
        {
            Display_printf(display, 0, 0,"[line:%d, error code:%d] %s\n\r", __LINE__, status, DEVICE_ERROR);
        }

        mode = sl_Start(0, 0, 0);
        if (mode < 0)
        {
            Display_printf(display, 0, 0,"[line:%d, error code:%d] %s\n\r", __LINE__, mode, DEVICE_ERROR);
        }
    }

    if(mode != ROLE_STA)
    {
        printError("Failed to configure device to it's default state\n\r", mode);
    }

    Display_printf(display, 0, 0,"Sl_Task Started Successfully\r\n");

    TAMUConnect();                                                                           /*TAMUConnect for Demo*/
    sem_post(&sem);
}
