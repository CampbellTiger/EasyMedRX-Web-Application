#ifndef JSON_MAKE_H
#define JSON_MAKE_H

#include <stdint.h>
#include <stddef.h>

#include <ti/utils/json/json.h>
#include <ti/drivers/net/wifi/simplelink.h>
#include <ti/display/Display.h>

/* ============================================================
 *  Build / size constants
 * ============================================================ */
#define APPLICATION_NAME        "JSON"
#define APPLICATION_VERSION     "1.0.0"
#define SL_STOP_TIMEOUT         (200)
#define SPAWN_TASK_PRIORITY     (9)
#define TASKSTACKSIZE           (4096)
#define JSON_SIZE               (2048)
#define TEMPLATE_SIZE           (2048)
#define MAX_SIZE                (2048)
#define MAX_LINE_WIDTH          (60)

/* ============================================================
 *  File-system filenames
 * ============================================================ */
#define TEMPLATE_FILENAME       "template1"
#define ERROR_TEMPLATE_FILENAME "errorTemplate"
#define JSON_FILENAME1          "json1"
#define JSON_FILENAME2          "json2"
#define JSON_FILENAME3          "json3"
#define JSON_FILENAME4          "json4"
#define NUM_JSON_FILES          4
#define COMP_STOCK_FILENAME     "compStock"
#define COMP_STOCK_TEMPLATE     "compStockTemplate"
#define TRUSTED_UID_FILENAME    "trustedUIDs"
#define TRUSTED_UID_TEMPLATE    "trustedUIDTemplate"
#define ONLINE_LOGIN_FILENAME   "onlineLogin"
#define TRUSTED_UID_MAX_COUNT   8   /* uid0-uid7 fields in the template;
                                     * cache and loop bound must not exceed this */
#define NUM_DEVICE_SLOTS        5   /* device0-device4 fields in the UpdateMCU template */
#define NUM_SCRIPT_SLOTS        4   /* script0-script3 / dose0-dose3 fields in the profile template */
#define WEB_APP_UPDATE_FILENAME      "webAppUpdate"   /* transient FS file used to POST updateWebApp payloads    */
/* STOCK_REQUEST_FILENAME removed — updateMCUStock no longer used.
 * The server responds to updateWebAppStock with its authoritative stock. */
#define NEW_PROFILE_REQUEST_FILENAME "newProfile"     /* transient FS file used to POST new-UID profile requests  */

/* ============================================================
 *  Misc constants
 * ============================================================ */
#define ASCI_0              48
#define ASCI_9              57
#define OBJ_BUFFER_SIZE     6
#define CMD_BUFFER_SIZE     100
#define SELECT_BUFFER_SIZE  2

/* ============================================================
 *  Types
 * ============================================================ */

/**
 * @brief Identifies which field set build() should print.
 *
 *   TEMPLATE_UPDATE_MCU  — user, pword, email, script0-3,
 *                          dose0-3, stock0-3
 *   TEMPLATE_ERROR       � type, user, message (string); time (int)
 *   TEMPLATE_COMP_STOCK  � medicine0-3 (string); dose0-3, stock0-3 (int32)
 *                          Global machine file, not tied to any user.
 *   TEMPLATE_TRUSTED_UID � uid0-uid7 (string); device access whitelist
 *   TEMPLATE_UNKNOWN     � default / plain JSON data buffer;
 *                          build() skips field printing and warns
 */
typedef enum
{
    TEMPLATE_UNKNOWN     = 0,
    TEMPLATE_UPDATE_MCU  = 1,
    TEMPLATE_ERROR       = 2,
    TEMPLATE_COMP_STOCK  = 3,   /* global compartment stock file */
    TEMPLATE_TRUSTED_UID = 4    /* device access whitelist: uid0-uid7 */
} Json_TemplateType_t;

/**
 * @brief Holds a pointer to a JSON / template string, its byte length,
 *        and the template type used by build() for auto-detection.
 *
 * fileSize should NOT include the null terminator.
 * Set templateType to TEMPLATE_UNKNOWN for plain JSON data buffers.
 */
typedef struct
{
    char               *fileBuffer;   /**< Pointer to the JSON/template string */
    uint16_t            fileSize;     /**< Length of the string (no null term) */
    Json_TemplateType_t templateType; /**< Identifies field set for build()    */
} Json_Filename_t;

/* ============================================================
 *  Pre-defined template / JSON objects
 *  (defined in json_make.c, declared extern here so any
 *   translation unit that includes this header can reference them)
 * ============================================================ */

/** Full UpdateMCU template (type, uid, user, pword, email,
 *  script0-3, dose0-3, stock0-3, time) */
extern Json_Filename_t templateFile;

/** Lightweight error-message template (type, uid, message, time) */
extern Json_Filename_t errorTemplate;

/**
 * @brief Global compartment stock template (medicine0-3, dose0-3, stock0-3).
 *        Not tied to any user � one file for the whole machine.
 *        This is the authoritative runtime stock counter; decrement
 *        stock# here on every successful dispense.
 */
extern Json_Filename_t compStockTemplate;

/** Trusted UID whitelist template (type, uid0-uid7). */
extern Json_Filename_t trustedUIDTemplate;

/** Example UpdateMCU JSON string (compile-time constant) */
extern const char *jsonString;

/** Example compartment stock JSON string (compile-time constant) */
extern const char *compStockJsonString;

/** Default trusted UID JSON string (compile-time constant) */
extern const char *trustedUIDJsonString;

/** Array of the four on-flash JSON data filenames */
extern const char *jsonFiles[NUM_JSON_FILES];

/* ============================================================
 *  Shared handles / state
 *  (defined in json_make.c)
 * ============================================================ */
extern Json_Handle  jsonHandle;       /**< Generic spare handle (unused = 0)  */
extern Json_Handle  templateHandle;   /**< Active UpdateMCU template handle   */
extern Json_Handle  jsonObjHandle;    /**< Active UpdateMCU object handle     */

/** Live compStock handles — persist for the lifetime of the program.
 *  Initialised once in jsonThread(); never destroyed.
 *  Use getCompartStock() / decrementCompartStock() to access them safely. */
extern Json_Handle  csObjHandle;       /**< Live compStock JSON object handle  */
extern Json_Handle  csTmplHandle;      /**< compStock template handle          */

/** Trusted UID handles — used only during jsonThread() init; destroyed after.
 *  isTrustedUID() reads from g_trustedUIDs[] cache, not these handles. */
extern Json_Handle  trustedObjHandle;  /**< trustedUIDs JSON object handle (init only) */
extern Json_Handle  trustedTmplHandle; /**< trustedUIDs template handle (init only)    */

extern Json_Filename_t templateBuff;  /**< Working template buffer            */
extern Json_Filename_t jsonBuffer;    /**< Working JSON input buffer          */
extern Json_Filename_t grabBuffer;    /**< Buffer filled by grabJson()        */

extern int16_t  templateSize;         /**< Byte count of last read template   */
extern uint16_t objSize;              /**< Current JSON object buffer size    */

/**
 * @brief FS path of the currently logged-in user's JSON file.
 *
 * Set by RC522_senseRFID() after a successful UID-to-filename lookup.
 * NULL when no user is logged in.
 * Read by dispense() / decrementCompartStock() to route error reports.
 */
extern const char *activeFile;

/**
 * @brief Session state flag.
 *
 *   1 = a user is logged in, LCD menu is active.
 *   0 = no session, RC522_senseRFID() is polling for a card.
 *
 * Set to 1 by RC522_senseRFID() on a successful UID match.
 * Set to 0 by cursorSelect() when Exit is selected on MENU_MAIN.
 * trackADC() exits its loop when this reaches 0.
 */
extern volatile uint8_t sessionActive;

/**
 * @brief Set to 1 once jsonThread() has completed initialisation.
 * storeJsonToFile() will not call httpPost() until this is 1,
 * preventing network calls while the semaphore is held at boot.
 */
extern volatile uint8_t systemReady;

/* ============================================================
 *  External dependencies (provided by other modules)
 * ============================================================ */
extern Display_Handle display;
extern int httpPost(const char *filename);
extern int httpPostGetResponse(const char *filename,
                               char *respBuf, uint16_t respBufLen);

/* ============================================================
 *  File-system helpers
 * ============================================================ */

/**
 * @brief Read a SimpleLink FS file into pBufferFile->fileBuffer.
 *
 * @param pBufferFile  Destination struct; fileBuffer is set internally.
 * @param FileName     Null-terminated FS filename.
 * @return Bytes read on success, negative error code on failure.
 */
int16_t readFile(Json_Filename_t *pBufferFile, char *FileName);

/**
 * @brief Return the real JSON string length of a stored FS file.
 *
 * SLFS pads files to their allocated size, so this scans backwards
 * for the closing '}' rather than relying on a null terminator.
 *
 * @param filename  Null-terminated FS filename.
 * @return Byte count up to and including '}', or 0 on error.
 */
uint16_t getJsonStrLen(const char *filename);

/**
 * @brief Read a JSON FS file and return a pointer to its contents.
 *
 * The pointer is valid until the next call to grabJson().
 *
 * @param filename  Null-terminated FS filename.
 * @return Pointer to the null-terminated JSON string, or NULL on error.
 */
char* grabJson(char *filename);

/* ============================================================
 *  Template management
 * ============================================================ */
void createTemplate(Json_Filename_t *pTemplateBuff, Json_Handle *pTemplateHandle);
void storeTemplate(Json_Filename_t *pTemplateBuff, const char *filename);
void destroyTemplate(Json_Handle *pTemplateHandle);

/* ============================================================
 *  JSON object management
 * ============================================================ */
void createObject(Json_Handle tmplHandle, Json_Handle *pObjHandle, uint16_t size);
void parse(Json_Handle objHandle, Json_Filename_t *pJsonBuff);

/**
 * @brief Build a JSON string from an object, display it, and optionally store it.
 *
 * Field printing is auto-detected from the global templateBuff.templateType:
 *   - TEMPLATE_UPDATE_MCU  : prints user, pword, email, script0-3 (string)
 *                            and dose0-3, stock0-3 (int32)
 *   - TEMPLATE_ERROR       : prints type, user, message (string) and time (int32)
 *   - TEMPLATE_COMP_STOCK  : prints medicine0-3 (string) and dose0-3, stock0-3 (int32)
 *   - TEMPLATE_UNKNOWN     : pretty-prints the raw JSON only; skips field dump
 *
 * Storage behaviour:
 *   - NULL                : display only, nothing written to flash
 *   - TEMPLATE_UPDATE_MCU : routes through storeJson() for credential-slot logic
 *   - All other types     : writes directly via storeJsonToFile()
 */
void storeJsonToFile(const char *filename, char *jsonStr, uint16_t size);
void build(Json_Handle objHandle, uint16_t *pObjSize, const char *storeFilename);
void destroyJsonObject(Json_Handle *pObjHandle);

/* ============================================================
 *  Compartment stock helpers
 * ============================================================ */

/**
 * @brief Return the current stock count for one compartment.
 *
 * Reads from the live global csObjHandle � no file I/O.
 *
 * @param compartment  0-based compartment index (0�3).
 * @return Current pill count, or -1 on error.
 */
int32_t getCompartStock(int compartment);

/**
 * @brief Return the dose count for one compartment.
 *
 * Reads from the live global csObjHandle � no file I/O.
 *
 * @param compartment  0-based compartment index (0�3).
 * @return Dose pill count, or -1 on error.
 */
int32_t getCompartDose(int compartment);
int getCompartMedicine(int compartment, char *buf, uint16_t bufLen);
int getScriptName(int slot, char *buf, uint16_t bufLen);
int getDeviceName(int slot, char *buf, uint16_t bufLen);
int32_t getProfileDose(int slot);
int openActiveProfile(void);
void closeActiveProfile(void);
uint8_t matchCompartments(void);
int decrementCompartStockByOne(int compartment, const char *pActiveFile);
int32_t getSessionWindow(int slot);
void incrementDispPills(int compartment);
/**
 * @brief  POST the compStock file (updateWebAppStock) to the server and
 *         refresh csObjHandle + FS file from the server's response.
 *         The server responds to updateWebAppStock with its authoritative
 *         stock values — no separate updateMCUStock message needed.
 *         Blocks the calling thread until the HTTP worker completes.
 *         Called from dispense() after PWM_stop() while servo is idle.
 */
void syncStockWithServer(void);

/**
 * @brief  Synchronously POST the profile file to the server and overwrite
 *         the FS file with the server's response (which carries live
 *         window0-3 fields).  Blocks the calling thread until complete.
 *
 *         Call this from RC522_senseRFID() after uidToFilename() finds a
 *         match, BEFORE setting sessionActive = 1 and spawning bootLCD.
 *         openActiveProfile() will then read server-fresh window data.
 *
 * @param  filename  The profile FS filename (e.g. "json1").
 * @return  0 on success, -1 on failure (FS file left unchanged).
 */
int syncProfileFromServer(const char *filename);

/**
 * @brief  Request a full profile from the server for a trusted UID that has
 *         no matching local profile file (first-time registration).
 *
 *         Sends {"type":"updateMCUProfile","uid":"<uid>"} to the server and
 *         blocks until a response arrives.  The response is validated:
 *           - Must be a well-formed JSON object with a "type" field.
 *           - Must contain non-empty "user" AND "pword" fields; if either
 *             is absent or empty the response is rejected (unknown user).
 *         On success the profile is written into the first free json slot
 *         (empty UID preferred, stale/deregistered UID as fallback).
 *
 * @param  uid      UID string from the RFID scan (e.g. "a1b2c3d4").
 * @return Pointer into jsonFiles[] naming the slot written (e.g. "json2"),
 *         or NULL on any failure.  Caller sets activeFile = return value
 *         and sessionActive = 1 then spawns bootLCD.
 */
const char *requestNewProfile(const char *uid);

/**
 * @brief Poll the server for a UID submitted from the web application.
 *
 * POSTs {"type":"onlineLogin","uid":""} to the server.  If the response
 * contains a non-empty "uid" field the matching profile is loaded (or a
 * new profile is requested) and a session is started identically to a
 * physical RFID card scan.
 *
 * Must only be called when sessionActive == 0.
 *
 * @return  1  session started (activeFile and sessionActive set).
 *          0  no pending login (empty uid in response).
 *         -1  HTTP or parse failure.
 */
int onlineLogin(void);

/**
 * @brief Decrement the stock of one compartment by its dose value and
 *        flush the updated object to the "compStock" FS file.
 *
 * If stock reaches zero after decrement, calls
 * logError(OutOfPills, activeFile) automatically.
 *
 * @param compartment  0-based compartment index (0�3).
 * @param pActiveFile  FS path of the logged-in user's file, used only
 *                     for error reporting.  May be NULL.
 * @return 0 on success, -1 if stock was already 0 or on error.
 */
int decrementCompartStock(int compartment, const char *pActiveFile);

/* ============================================================
 *  Credential / UID helpers
 * ============================================================ */
int checkCreds(const char *filename);
void storeJson(char *jsonString, uint16_t size);
uint8_t compUID(char *UID, const char *filename);
const char* uidToFilename(char *UID);
int isTrustedUID(const char *uid);

/* ============================================================
 *  Display helpers
 * ============================================================ */
void removeUnwantedChars(char *pBuf);
void prettyDisplay(char *json);

/* ============================================================
 *  Thread entry point
 * ============================================================ */

/**
 * @brief Main JSON task thread.
 *
 * Initialises both the UpdateMCU and compStock flows, then releases
 * the semaphore.  csObjHandle and csTmplHandle remain live after this
 * returns so that getCompartStock() / decrementCompartStock() work at
 * any time.
 *
 * @param arg  Unused (POSIX thread argument).
 * @return NULL.
 */
void* jsonThread(void *arg);

/**
 * @brief Load compile-time default JSON strings to the SimpleLink FS.
 *
 *        Writes json1, compStock, and trustedUIDs files if they don't
 *        already exist. Skips files that are already present to avoid
 *        overwriting user data or server-synced updates.
 *
 * @return 0 on success, -1 if any file write fails.
 */
int loadDummyFiles(void);

#endif /* JSON_MAKE_H */
