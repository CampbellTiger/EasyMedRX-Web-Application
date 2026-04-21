#ifndef HTTP_QUEUE_H
#define HTTP_QUEUE_H

/*===========================================================================
 *  http_queue.h
 *
 *  Single-consumer FIFO queue that serialises all NWP HTTP activity.
 *  Any thread enqueues a request; the dedicated httpWorkerThread (prio 4)
 *  dequeues and executes them one at a time, guaranteeing no concurrent
 *  HTTPClient calls reach the CC3220S NWP.
 *
 *  Two request types:
 *
 *    HTTP_Q_POST          -- fire-and-forget.  Caller continues immediately.
 *                           Used by storeJsonToFile() and RC522_senseRFID().
 *
 *    HTTP_Q_POST_GET      -- synchronous.  Caller blocks until the worker
 *                           completes and signals done_sem.  Response is
 *                           written into the caller's respBuf.
 *                           Used by flushErrorBuffer().
 *===========================================================================*/

#include <stdint.h>
#include <semaphore.h>

/* Maximum filename length (matches FS slot names: "json1", "error0" etc.) */
#define HTTP_Q_FILENAME_MAX   16

/* Queue depth -- power of two for cheap modulo */
#define HTTP_Q_DEPTH          8

typedef enum
{
    HTTP_Q_POST     = 0,   /* httpPost(filename)                   -- fire-and-forget */
    HTTP_Q_POST_GET = 1    /* httpPostGetResponse(filename,buf,len) -- synchronous     */
} HttpQ_Type;

typedef struct
{
    HttpQ_Type  type;
    char        filename[HTTP_Q_FILENAME_MAX];

    /* HTTP_Q_POST_GET only -- caller-allocated, worker writes result */
    char       *respBuf;
    uint16_t    respBufLen;
    int        *retCode;      /* worker stores return value here before posting done */
    sem_t      *done;         /* worker posts this when response is ready */
} HttpQ_Entry;

/* -----------------------------------------------------------------------
 *  Public API
 * -------------------------------------------------------------------- */

/**
 * @brief  Enqueue a fire-and-forget POST.  Non-blocking.
 *         Returns 0 on success, -1 if the queue is full (request dropped).
 */
int httpQ_post(const char *filename);

/**
 * @brief  Enqueue a POST-and-get-response.  Blocks until the worker
 *         completes.  Return value mirrors httpPostGetResponse().
 */
int httpQ_postGetResponse(const char *filename,
                          char *respBuf, uint16_t respBufLen);

/**
 * @brief  Initialise the queue mutex and semaphore.
 *         Must be called from schedFuncs() before any thread that may
 *         call httpQ_post() or httpQ_postGetResponse() is spawned.
 */
void httpQ_init(void);

/**
 * @brief  Worker thread entry point.  Spawn once from schedFuncs() at prio 4.
 */
void *httpWorkerThread(void *pvParameters);

#endif /* HTTP_QUEUE_H */

