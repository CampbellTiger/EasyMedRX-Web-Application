#include "json_make.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  Word tables (ROM-resident)                                          */
/* ------------------------------------------------------------------ */

const char* baseName = "error";

void* genErrName(){
    char fileSize[10];
    char* filename;
    char* str;
    int32_t Status = 0;
    SlFsFileInfo_t FsFileInfo;

    /*100 max error buffers between all accounts*/
    for (int i = 0; i<100; i++)
    {
        /*convert int to sting*/
        snprintf(str, sizeof(fileSize), "%d", i);
        strcat(filename, baseName);
        strcat(filename, baseName);

        Status = sl_FsGetInfo((unsigned char *)filename,0,&FsFileInfo);
        if(Status < 0)
        {
            Display_printf(display, 0, 0, "File does not exist, Status: %d \n\r", Status);
            break;
        }

        filename = "";

        /*check if file exists*/
        if(i == 99)
        {
            /*No more space*/
        }
    }

    return NULL;

}



