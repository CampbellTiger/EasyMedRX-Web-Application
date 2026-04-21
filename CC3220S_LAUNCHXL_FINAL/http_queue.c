/*===========================================================================
 *  http_queue.c
 *
 *  Single-producer-multiple-consumer-safe? No -- single consumer only.
 *  Multiple producers (any thread) can enqueue concurrently because
 *  enqueue is protected by a POSIX mutex.  The worker is the sole
 *  consumer and never needs to lock -- it owns the read index.
 *===========================================================================*/

#include <string.h>
#include <semaphore.h>
#include <pthread.h>
#include <ti/display/Display.h>

#include "http_queue.h"
#include "json_make.h"    /* httpPost(), httpPostGetResponse() */

extern Display_Handle display;

/* -----------------------------------------------------------------------
 *  Internal queue state
 * -------------------------------------------------------------------- */
static HttpQ_Entry   g_queue[HTTP_Q_DEPTH];
static volatile int  g_head = 0;   /* producer writes here */
static volatile int  g_tail = 0;   /* consumer reads here  */

static pthread_mutex_t g_qMutex;   /* protects g_head and enqueue writes */
static sem_t           g_qSem;     /* counts pending entries (worker waits here) */

/* -----------------------------------------------------------------------
 *  httpQ_post()  -- fire-and-forget
 * -------------------------------------------------------------------- */
int httpQ_post(const char *filename)
{
    Display_printf(display, 0, 0, "[ENTER] httpQ_post\n\r");
    if (filename == NULL) {
        Display_printf(display, 0, 0, "httpQ_post: NULL filename\n\r");
        return -1;
    }
    pthread_mutex_lock(&g_qMutex);

    int next = (g_head + 1) & (HTTP_Q_DEPTH - 1);
    if (next == g_tail)
    {
        /* Queue full -- drop the request */
        pthread_mutex_unlock(&g_qMutex);
        Display_printf(display, 0, 0,
            "httpQ_post: queue full, dropping POST for %s\n\r", filename);
        return -1;
    }

    HttpQ_Entry *e = &g_queue[g_head];
    e->type       = HTTP_Q_POST;
    e->respBuf    = NULL;
    e->respBufLen = 0;
    e->retCode    = NULL;
    e->done       = NULL;
    strncpy(e->filename, filename, HTTP_Q_FILENAME_MAX - 1);
    e->filename[HTTP_Q_FILENAME_MAX - 1] = '\0';

    g_head = next;
    pthread_mutex_unlock(&g_qMutex);

    sem_post(&g_qSem);   /* wake worker */
    return 0;
}

/* -----------------------------------------------------------------------
 *  httpQ_postGetResponse()  -- synchronous (caller blocks on done_sem)
 * -------------------------------------------------------------------- */
int httpQ_postGetResponse(const char *filename,
                          char *respBuf, uint16_t respBufLen)
{
    Display_printf(display, 0, 0, "[ENTER] httpQ_postGetResponse\n\r");
    if (filename == NULL) {
        Display_printf(display, 0, 0, "httpQ_postGetResponse: NULL filename\n\r");
        return -1;
    }
    int       retCode = -1;
    sem_t     done;
    sem_init(&done, 0, 0);

    pthread_mutex_lock(&g_qMutex);

    int next = (g_head + 1) & (HTTP_Q_DEPTH - 1);
    if (next == g_tail)
    {
        pthread_mutex_unlock(&g_qMutex);
        sem_destroy(&done);
        Display_printf(display, 0, 0,
            "httpQ_postGetResponse: queue full, dropping GET for %s\n\r", filename);
        return -1;
    }

    HttpQ_Entry *e = &g_queue[g_head];
    e->type       = HTTP_Q_POST_GET;
    e->respBuf    = respBuf;
    e->respBufLen = respBufLen;
    e->retCode    = &retCode;
    e->done       = &done;
    strncpy(e->filename, filename, HTTP_Q_FILENAME_MAX - 1);
    e->filename[HTTP_Q_FILENAME_MAX - 1] = '\0';

    g_head = next;
    pthread_mutex_unlock(&g_qMutex);

    sem_post(&g_qSem);    /* wake worker */
    sem_wait(&done);      /* block until worker completes this request */
    sem_destroy(&done);

    return retCode;
}

/* -----------------------------------------------------------------------
 *  httpQ_init()  -- call from schedFuncs() before any producer thread runs
 * -------------------------------------------------------------------- */
void httpQ_init(void)
{
    pthread_mutex_init(&g_qMutex, NULL);
    sem_init(&g_qSem, 0, 0);
}

/* -----------------------------------------------------------------------
 *  httpWorkerThread()  -- single consumer, prio 4
 * -------------------------------------------------------------------- */
void *httpWorkerThread(void *pvParameters)
{
    Display_printf(display, 0, 0, "[ENTER] httpWorkerThread\n\r");
    Display_printf(display, 0, 0, "httpWorkerThread: ready\n\r");

    while (1)
    {
        /* Block until at least one entry is available */
        sem_wait(&g_qSem);

        /* Consume one entry -- no lock needed, we are the only consumer */
        HttpQ_Entry *e = &g_queue[g_tail];
        g_tail = (g_tail + 1) & (HTTP_Q_DEPTH - 1);

        Display_printf(display, 0, 0,
            "httpWorkerThread: executing %s for %s\n\r",
            e->type == HTTP_Q_POST ? "POST" : "POST_GET",
            e->filename);

        if (e->type == HTTP_Q_POST)
        {
            httpPost(e->filename);
            /* Return value discarded -- fire-and-forget */
        }
        else /* HTTP_Q_POST_GET */
        {
            int ret = httpPostGetResponse(e->filename,
                                          e->respBuf,
                                          e->respBufLen);
            if (e->retCode) *e->retCode = ret;
            if (e->done)     sem_post(e->done);   /* unblock caller */
        }
    }

    return NULL;
}
