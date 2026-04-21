#ifndef ERROR_HANDLER_H
#define ERROR_HANDLER_H

#include <stdint.h>
#include "json_make.h"

/* ============================================================
 *  Error codes
 * ============================================================ */
typedef enum {
    DispensingError  = -5,
    OutOfPills       = -4,
    Disconnected     = -3,
    FatalParsing     = -1,
    Connected        =  0
} error_Code;

extern error_Code errorState;

#define MAX_ERROR_FILES    100
#define ERROR_BASE_NAME    "error"

/**
 * @brief Build an error JSON and store it to FS (auto-POSTs via storeJsonToFile).
 *
 * @param retCode      error_Code value.
 * @param pActiveFile  FS path of logged-in user's file ("json1" etc.), or NULL.
 * @param compartment  0-based compartment index; pass -1 when not applicable.
 */
void parseErrorMessage(int retCode, const char *pActiveFile, int compartment);

/**
 * @brief Top-level error dispatcher.  Updates errorState and routes to handler.
 *
 * @param retCode      error_Code value.
 * @param pActiveFile  FS path of logged-in user's file, or NULL.
 * @param compartment  0-based compartment index; pass -1 when not applicable.
 */
void logError(int retCode, const char *pActiveFile, int compartment);

/**
 * @brief Scan all error slots, POST each to the server, and delete
 *        any file where the server responds with return_code == 0.
 */
void flushErrorBuffer(void);

#endif /* ERROR_HANDLER_H */
