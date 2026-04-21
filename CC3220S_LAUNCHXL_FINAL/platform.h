#ifndef PLATFORM_H_
#define PLATFORM_H_

/*===========================================================================*/
/*  Includes                                                                 */
/*===========================================================================*/
#include <stdint.h>
#include <stdbool.h>

#include <ti/drivers/net/wifi/simplelink.h>
#include <ti/drivers/net/wifi/slnetifwifi.h>
#include <ti/display/Display.h>
#include "pthread.h"
#include <semaphore.h>

/*===========================================================================*/
/*  Application Constants                                                    */
/*===========================================================================*/
#define APPLICATION_NAME1           "HTTP GET"
#define APPLICATION_NAME2           "HTTP POST"

/*===========================================================================*/
/*  Error String Constants                                                   */
/*===========================================================================*/
#define DEVICE_ERROR                ("Device error, please refer \"DEVICE ERRORS CODES\" section in errors.h")
#define WLAN_ERROR                  ("WLAN error, please refer \"WLAN ERRORS CODES\" section in errors.h")

/*===========================================================================*/
/*  Task / Thread Configuration                                              */
/*===========================================================================*/
#define SL_STOP_TIMEOUT             (200)
#define SPAWN_TASK_PRIORITY         (9)
#define SPAWN_STACK_SIZE            (4096)
#define TASK_STACK_SIZE             (2048)

/*===========================================================================*/
/*  SlNet Configuration                                                      */
/*===========================================================================*/
#define SLNET_IF_WIFI_PRIO          (5)
#define SLNET_IF_WIFI_NAME          "CC32xx"

/*===========================================================================*/
/*  WiFi Credentials                                                         */
/*===========================================================================*/
#define SSID_NAME                   ""
#define SECURITY_TYPE               SL_WLAN_SEC_TYPE_WPA_WPA2
#define SECURITY_KEY                ""
#define TAMU_IoT_NAME               "TAMU_IoT"

/*===========================================================================*/
/*  Device Identity                                                          */
/*===========================================================================*/
#define DEVICE_NAME                 "CC3220S_GA"

/*===========================================================================*/
/*  Hostname Parameter ID fallback                                           */
/*===========================================================================*/
#ifndef SL_NETCFG_HOSTNAME
#define SL_NETCFG_HOSTNAME          0x2003   /* hostname parameter ID */
#endif

/*===========================================================================*/
/*  Extern Globals (defined in platform.c)                                  */
/*===========================================================================*/

/** Station MAC address bytes populated by TAMUConnect(). */
extern unsigned char g_staMac[6];

/** Set to true once g_staMac has been successfully read. */
extern bool g_staMacValid;

/**
 * @brief   Return code from the most recent HTTPClient_connect() attempt.
 *
 * Set by httpConnect() in http_comm.c after every connect attempt.
 * Notable values:
 *   >= 0   success
 *   -111   ECONNREFUSED — WiFi up but server actively refused the connection
 *   -2006  EALREADY     — NWP socket not yet released from previous session
 */
extern int16_t g_lastHttpConnectRet;

/** Handle for the HTTP worker thread. */
extern pthread_t httpThread;

/** Handle for the SimpleLink spawn thread. */
extern pthread_t spawn_thread;

/** Current SimpleLink operating mode (ROLE_STA, ROLE_AP, etc.). */
extern int32_t mode;

/** Display driver handle shared across translation units. */
extern Display_Handle display;

/*===========================================================================*/
/*  External Dependencies                                                    */
/*===========================================================================*/

/** SlNet initialisation � implemented in the TI SDK network layer. */
extern int32_t ti_net_SlNet_initConfig(void);

/** Semaphore used to signal the JSON / HTTP thread after IP acquisition. */
extern sem_t sem;

/*===========================================================================*/
/*  Function Prototypes                                                      */
/*===========================================================================*/

/**
 * @brief   Print an error message and halt execution in an infinite loop.
 *
 * @param   errString   Human-readable description of the error.
 * @param   code        Numeric error code to display alongside the message.
 */
void printError(char *errString, int code);

/**
 * @brief   Connect to a WPA/WPA2 protected access point using the credentials
 *          defined by SSID_NAME and SECURITY_KEY.
 *
 * @return  0 on success; negative SimpleLink error code on failure.
 */
int Connect(void);

/**
 * @brief   Connect to the TAMU_IoT network using MAC-based authorisation.
 *
 *          Sets the device hostname to DEVICE_NAME, reads and displays the
 *          station MAC address, then issues a WPA/WPA2 connection request
 *          to TAMU_IoT_NAME with no security parameters.
 *
 * @return  0 on success; negative SimpleLink error code on failure.
 */
int TAMUConnect(void);

/**
 * @brief   Reconnect to TAMU_IoT if the last HTTP connect attempt was refused.
 *
 *          Checks g_lastHttpConnectRet; if it equals -111 (ECONNREFUSED),
 *          issues sl_WlanDisconnect() followed by TAMUConnect() to force a
 *          fresh association with the AP.  No-op if the last code was anything
 *          other than -111.
 *
 *          Call from a normal thread context only — not from a SimpleLink
 *          event handler.
 */
void checkAndReconnect(void);

/**
 * @brief   Main application thread entry point.
 *
 *          Opens the UART display, starts the SimpleLink spawn thread,
 *          initialises the NWP in STA mode, connects to the network via
 *          TAMUConnect(), and posts the global semaphore to release the
 *          JSON / HTTP thread.
 *
 * @param   pvParameters    Unused thread argument.
 */
void mainThread(void *pvParameters);

/*===========================================================================*/
/*  SimpleLink Event Handler Prototypes                                      */
/*  (Required callbacks registered with the SimpleLink host driver)         */
/*===========================================================================*/

/**
 * @brief   Handles IPv4/IPv6 acquisition events and initialises SlNetSock.
 * @param   pNetAppEvent    Pointer to the NetApp event data structure.
 */
void SimpleLinkNetAppEventHandler(SlNetAppEvent_t *pNetAppEvent);

/**
 * @brief   Called by the host driver on a fatal NWP error.
 *          The application must restart the device to recover.
 * @param   slFatalErrorEvent   Pointer to the fatal error event structure.
 */
void SimpleLinkFatalErrorEventHandler(SlDeviceFatal_t *slFatalErrorEvent);

/**
 * @brief   Called when the NWP has finished with a NetApp request buffer.
 * @param   buffer  Pointer to the buffer that may now be freed.
 */
void SimpleLinkNetAppRequestMemFreeEventHandler(uint8_t *buffer);

/**
 * @brief   Called on incoming NetApp requests from the NWP.
 * @param   pNetAppRequest   Pointer to the request structure.
 * @param   pNetAppResponse  Pointer to the response structure.
 */
void SimpleLinkNetAppRequestEventHandler(SlNetAppRequest_t *pNetAppRequest,
                                         SlNetAppResponse_t *pNetAppResponse);

/**
 * @brief   Called on HTTP server events from the NWP's internal HTTP server.
 * @param   pHttpEvent    Pointer to the HTTP event data.
 * @param   pHttpResponse Pointer to the HTTP response data.
 */
void SimpleLinkHttpServerEventHandler(
    SlNetAppHttpServerEvent_t *pHttpEvent,
    SlNetAppHttpServerResponse_t *pHttpResponse);

/**
 * @brief   Handles WLAN events such as connect and disconnect.
 * @param   pWlanEvent  Pointer to the WLAN event data structure.
 */
void SimpleLinkWlanEventHandler(SlWlanEvent_t *pWlanEvent);

/**
 * @brief   Handles general device events from the NWP.
 * @param   pDevEvent   Pointer to the device event structure.
 */
void SimpleLinkGeneralEventHandler(SlDeviceEvent_t *pDevEvent);

/**
 * @brief   Handles socket events reported by the NWP / host driver.
 * @param   pSock   Pointer to the socket event data structure.
 */
void SimpleLinkSockEventHandler(SlSockEvent_t *pSock);

/**
 * @brief   Print a formatted banner to the display with the application name.
 *
 * @param   AppName     Null-terminated string of the application name
 *                      to display in the banner.
 */
void DisplayBanner(char *AppName);

#endif /* PLATFORM_H_ */
