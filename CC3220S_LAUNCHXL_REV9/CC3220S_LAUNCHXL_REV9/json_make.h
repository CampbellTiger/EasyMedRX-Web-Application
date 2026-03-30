#ifndef JSON_MAKE_H
#define JSON_MAKE_H

/*===========================================================================*/
/*  Includes                                                                 */
/*===========================================================================*/
#include <stdint.h>
#include <stddef.h>

#include <ti/utils/json/json.h>
#include <ti/drivers/net/wifi/simplelink.h>
#include <ti/display/Display.h>
#include <semaphore.h>

/*===========================================================================*/
/*  Application Constants                                                    */
/*===========================================================================*/
#define APPLICATION_NAME        "JSON"
#define APPLICATION_VERSION     "1.0.0"

/*===========================================================================*/
/*  SimpleLink / Task Configuration                                          */
/*===========================================================================*/
#define SL_STOP_TIMEOUT         (200)
#define SPAWN_TASK_PRIORITY     (9)
#define TASKSTACKSIZE           (4096)

/*===========================================================================*/
/*  File System / Buffer Sizes                                               */
/*===========================================================================*/
#define JSON_SIZE               (2048)
#define TEMPLATE_SIZE           (2048)
#define MAX_SIZE                (2048)

/*===========================================================================*/
/*  File Names                                                               */
/*===========================================================================*/
#define TEMPLATE_FILENAME       "template1"
#define JSON_FILENAME1          "json1"
#define JSON_FILENAME2          "json2"
#define JSON_FILENAME3          "json3"
#define JSON_FILENAME4          "json4"
#define NUM_JSON_FILES          4

/*===========================================================================*/
/*  ASCII / UI Constants                                                     */
/*===========================================================================*/
#define ASCI_0                  48
#define ASCI_9                  57
#define OBJ_BUFFER_SIZE         6
#define CMD_BUFFER_SIZE         100
#define SELECT_BUFFER_SIZE      2
#define MAX_LINE_WIDTH          60

/*===========================================================================*/
/*  Return Codes for checkCreds()                                            */
/*===========================================================================*/
/* Return  2  -> username + password match (update existing record)          */
/* Return  1  -> file exists but is empty  (available slot)                  */
/* Return  0  -> file does not exist       (available slot)                  */
/* Return -1  -> username not found        (no match)                        */
/* Return -2  -> username match, wrong password (duplicate user, bad pass)   */
/* Return -3  -> internal / I/O error                                        */

/*===========================================================================*/
/*  Data Types                                                               */
/*===========================================================================*/

/**
 * @brief Holds a pointer to a JSON / template text buffer and its byte length.
 */
typedef struct
{
    char    *fileBuffer;    /*!< Pointer to the character buffer            */
    uint16_t fileSize;      /*!< Length of valid data in the buffer (bytes) */
} Json_Filename_t;

/*===========================================================================*/
/*  Extern Globals (defined in json.c)                                       */
/*===========================================================================*/

/** Pre-defined template describing all fields in the main JSON schema. */
extern Json_Filename_t templateFile;

/** Pre-defined template describing all fields in the error JSON schema. */
extern Json_Filename_t errorTemplate;

/** Example JSON payload used for initial parse / build testing. */
extern const char *jsonString;

/** Example error-event JSON payload. */
extern const char *errorExWebApp;

/** Ordered list of on-device JSON storage file names. */
extern const char *jsonFiles[NUM_JSON_FILES];

/* Active JSON / template handle objects */
extern Json_Handle jsonHandle;
extern Json_Handle templateHandle;
extern Json_Handle jsonObjHandle;

/* Working buffers */
extern Json_Filename_t templateBuff;
extern Json_Filename_t jsonBuffer;
extern Json_Filename_t grabBuffer;

/* Peripheral / OS handles shared across translation units */
extern Display_Handle display;
extern sem_t          sem;

/* Miscellaneous state */
extern int16_t  templateSize;
extern uint16_t objSize;

/*===========================================================================*/
/*  Function Prototypes                                                      */
/*===========================================================================*/

/**
 * @brief   Read a SimpleLink FS file into a Json_Filename_t buffer.
 *
 * @param   pBufferFile  Destination buffer struct; fileBuffer is set to the
 *                       internal static buffer and filled with file contents.
 * @param   FileName     Null-terminated name of the file to read.
 * @return  Number of bytes read on success; negative SimpleLink error code
 *          on failure.
 */
int16_t readFile(Json_Filename_t *pBufferFile, char *FileName);

/**
 * @brief   Strip newline ('\n') and space (' ') characters from a buffer
 *          in-place.
 *
 * @param   pBuf  Null-terminated string to clean up.
 */
void removeUnwantedChars(char *pBuf);

/**
 * @brief   Pretty-print a compact JSON string to the display with indentation
 *          and line wrapping at MAX_LINE_WIDTH.
 *
 * @param   json  Null-terminated JSON string to format and display.
 */
void prettyDisplay(char *json);

/**
 * @brief   Create a TI JSON template object from templateBuff.
 *          Stores the resulting handle in the global templateHandle.
 */
void createTemplate(void);

/**
 * @brief   Create a TI JSON object bound to the active template.
 *          Stores the resulting handle in the global jsonObjHandle.
 */
void createObject(void);

/**
 * @brief   Parse jsonBuffer into the active JSON object (jsonObjHandle).
 */
void parse(void);

/**
 * @brief   Get a value from the active JSON object by key.
 *
 * @param   pKey  JSON key string including surrounding quotes, e.g. "\"user\"".
 *
 * @note    This function prints the value to the display. It is currently
 *          used for boolean-style int32 fields.
 */
void getValue(char *pKey);

/**
 * @brief   Compare the credentials (user / pword) in the incoming JSON object
 *          against those stored in an on-device file.
 *
 * @param   filename  Name of the SimpleLink FS file to check.
 * @return   2  Username and password match.
 * @return   1  File is empty (available slot).
 * @return   0  File does not exist (available slot).
 * @return  -1  Username not found.
 * @return  -2  Username matched, password wrong.
 * @return  -3  I/O or JSON processing error.
 */
int checkCreds(const char *filename);

/**
 * @brief   Serialise the active JSON object and write it to the appropriate
 *          on-device file, creating a new record or overwriting an existing
 *          one as determined by checkCreds().
 *
 * @param   jsonString  Pointer to the JSON text to store.
 * @param   size        Maximum number of bytes to consider from jsonString.
 */
void storeJson(char *jsonString, uint16_t size);

/**
 * @brief   Build (serialise) the active JSON object to text, pretty-print it,
 *          display individual field values, then call storeJson().
 */
void build(void);

/**
 * @brief   Destroy the active TI JSON template (templateHandle).
 */
void destroyTemplate(void);

/**
 * @brief   Destroy the active TI JSON object (jsonObjHandle).
 */
void destroyJsonObject(void);

/**
 * @brief   Write the global templateFile buffer to the SimpleLink FS file
 *          named TEMPLATE_FILENAME.
 */
void storeTemplate(void);

/**
 * @brief   Compare a UID string against the "uid" field stored in a given
 *          on-device JSON file.
 *
 * @param   UID       Null-terminated UID string to look up.
 * @param   filename  Name of the SimpleLink FS file to inspect.
 * @return  1 if the UIDs match; 0 otherwise.
 */
uint8_t compUID(char *UID, const char *filename);

/**
 * @brief   Search all JSON storage files for a matching UID.
 *
 * @param   UID  Null-terminated UID string to find.
 * @return  Pointer to the file-name string constant where the UID was found,
 *          or NULL if no match exists.
 */
const char *uidToFilename(char *UID);

/**
 * @brief   Read a stored JSON file and return a pointer to its contents.
 *
 * @param   filename  Name of the SimpleLink FS file to read.
 * @return  Pointer to the internal grab buffer containing the file text.
 *
 * @note    The returned pointer is valid only until the next call to grabJson()
 *          or readFile().
 */
char *grabJson(char *filename);

/**
 * @brief   Determine the actual byte length of JSON content in a SimpleLink
 *          FS file by scanning backwards for the closing '}'.
 *
 * @param   filename  Name of the SimpleLink FS file.
 * @return  Byte length of the JSON string (including the closing brace),
 *          or 0 on error or if no closing brace is found.
 */
uint16_t getJsonStrLen(const char *filename);

/**
 * @brief   Main JSON thread entry point.
 *
 *          Waits on the global semaphore, then exercises the full
 *          template -> object -> parse -> build -> store -> retrieve pipeline.
 *
 * @param   arg  Unused thread argument.
 * @return  NULL.
 */
void *jsonThread(void *arg);

#endif /* JSON_H_ */
