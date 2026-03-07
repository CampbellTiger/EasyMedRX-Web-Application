#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>

#include <ti/utils/json/json.h>
#include <ti/drivers/net/wifi/simplelink.h>
#include <ti/display/Display.h>
#include <semaphore.h>

#define APPLICATION_NAME                        "JSON"
#define APPLICATION_VERSION                     "1.0.0"
#define SL_STOP_TIMEOUT                         (200)
#define SPAWN_TASK_PRIORITY                     (9)
#define TASKSTACKSIZE                           (4096)
#define JSON_SIZE                               (2048)
#define TEMPLATE_SIZE                           (2048)
#define MAX_SIZE                                (2048)
#define TEMPLATE_FILENAME                       "template1"
#define JSON_FILENAME1                          "json1"
#define JSON_FILENAME2                          "json2"
#define JSON_FILENAME3                          "json3"
#define JSON_FILENAME4                          "json4"
#define NUM_JSON_FILES                          4


#define ASCI_0                                  48
#define ASCI_9                                  57
#define OBJ_BUFFER_SIZE                         6
#define CMD_BUFFER_SIZE                         100
#define SELECT_BUFFER_SIZE                      2

#define MAX_LINE_WIDTH                          60

typedef struct
{
    char *fileBuffer;
    uint16_t fileSize;
} Json_Filename_t;

Json_Filename_t templateFile = {
        .fileBuffer =
        "{"
        "\"user\":string,"
        "\"pword\":string,"
        "\"email\":string,"
        "\"phone\":string,"
        "\"script0\":string,"
        "\"script1\":string,"
        "\"script2\":string,"
        "\"script3\":string,"
        "\"dose0\":int32,"
        "\"dose1\":int32,"
        "\"dose2\":int32,"
        "\"dose3\":int32,"
        "\"stock0\":int32,"
        "\"stock1\":int32,"
        "\"stock2\":int32,"
        "\"stock3\":int32"
        "}",

        .fileSize = sizeof(
        "{"
        "\"user\":string,"
        "\"pword\":string,"
        "\"email\":string,"
        "\"phone\":string,"
        "\"script0\":string,"
        "\"script1\":string,"
        "\"script2\":string,"
        "\"script3\":string,"
        "\"dose0\":int32,"
        "\"dose1\":int32,"
        "\"dose2\":int32,"
        "\"dose3\":int32,"
        "\"stock0\":int32,"
        "\"stock1\":int32,"
        "\"stock2\":int32,"
        "\"stock3\":int32"
        "}")-1};

const char* jsonString =
        "{"
        "\"user\":\"Bugs\","
        "\"pword\":\"Bunny\","
        "\"email\":\"DaphyDuck@gmail.com\","
        "\"phone\":\"904-393-9032\","
        "\"script0\":\"Tic-Tacs\","
        "\"script1\":\"Gobstoppers\","
        "\"script2\":\"Atomic Fireballs\","
        "\"script3\":\"Skittles\","
        "\"dose0\":10,"
        "\"dose1\":10,"
        "\"dose2\":100,"
        "\"dose3\":70,"
        "\"stock0\":5000,"
        "\"stock1\":1000,"
        "\"stock2\":3000,"
        "\"stock3\":170"
        "}";

const char *jsonFiles[NUM_JSON_FILES] = { JSON_FILENAME1, JSON_FILENAME2, JSON_FILENAME3, JSON_FILENAME4 };
static char setFSFileSize[MAX_SIZE];
static char setTempBuffSize[MAX_SIZE];
static char setBuildSize[MAX_SIZE];

/*.json Handles*/
Json_Handle jsonHandle = 0;                                                  /*=0 means handle object not yet created*/
Json_Handle templateHandle;
Json_Handle jsonObjHandle;
Json_Filename_t templateBuff;
Json_Filename_t jsonBuffer;

/*Global template requires global buffer*/
static char buff[JSON_SIZE];

/*Display handle*/
extern Display_Handle display;

/*Semaphore*/
extern sem_t sem;

/*Global variables*/
int16_t templateSize;
uint16_t objSize;

int16_t readFile(Json_Filename_t *pBufferFile,
                 char *FileName)
{
    int32_t fileHandle = 0;
    int32_t Status = 0;
    uint32_t FileSize;
    int16_t retVal = 0;

    SlFsFileInfo_t FsFileInfo;

    /* Get the file size */
    Status = sl_FsGetInfo((unsigned char *)FileName,0,&FsFileInfo);
    if(Status < 0)
    {
        Display_printf(display, 0, 0, "FS - Couldn't get info on file."
                                      " error status %d \n\r", Status);
        return(Status);
    }

    FileSize = FsFileInfo.Len;
    fileHandle = sl_FsOpen((unsigned char *)FileName, SL_FS_READ,0);

    if(fileHandle < 0)
    {
        Display_printf(display, 0, 0, "FS - Couldn't open file. error status %d \n\r",fileHandle);
        return(fileHandle);
    }

    else
    {
        pBufferFile->fileBuffer = setFSFileSize;

        memset(pBufferFile->fileBuffer,'\0',FileSize + 1);
        /* Read the entire file */
        Status = sl_FsRead(fileHandle, 0, (unsigned char *)pBufferFile->fileBuffer, FileSize);

        if(Status < 0)
        {
            Display_printf(display, 0, 0, "FS - Couldn't read file. error status %d \n\r", Status);
            sl_FsClose(fileHandle, NULL, NULL, 0);
            return(Status);
        }

        retVal = sl_FsClose(fileHandle,NULL,NULL,0);

        if(retVal < 0)
        {
            Display_printf(display, 0, 0, "FS - Couldn't close file. error status %d \n\r", retVal);
            return(retVal);
        }

        Display_printf(display, 0, 0, "Read %s succesfully\n\r", FileName);
        return(Status);
    }
}

void removeUnwantedChars(char *pBuf)
{
    char * str_tmp;
    uint16_t i = 0,j = 0;
    str_tmp = pBuf;

    for(i = 0; str_tmp[i] != '\0'; ++i)
    {
        while((!(str_tmp[i] != '\n') ||
               !(str_tmp[i] != ' '))&& (str_tmp[i] != '\0'))
        {
            for(j = i; str_tmp[j] != '\0'; ++j)
            {
                str_tmp[j] = str_tmp[j + 1];
            }
            str_tmp[j] = '\0';
        }
    }
}

void prettyDisplay(char *json)
{
    char prettyBuf[2048];
    int pos = 0;
    int linePos = 0;
    int indent = 0;
    int inString = 0;
    const char *prefix = "  ";  // Two spaces at beginning

    // Insert prefix at start of first line
    for (int k = 0; prefix[k] != '\0'; k++)
    {
        prettyBuf[pos++] = prefix[k];
        linePos++;
    }

    for (uint16_t i = 0; json[i] != '\0'; i++)
    {
        char c = json[i];

        // Track if inside string
        if (c == '"') inString = !inString;

        if (!inString)
        {
            if (c == '{' || c == '[')
            {
                prettyBuf[pos++] = c;
                prettyBuf[pos++] = '\r';  // carriage return at line end
                prettyBuf[pos++] = '\n';
                linePos = 0;
                indent++;

                /*Add prefix + indent at new line*/
                for (int j = 0; j < indent; j++) { prettyBuf[pos++] = ' '; linePos++; }
                for (int k = 0; prefix[k] != '\0'; k++) { prettyBuf[pos++] = prefix[k]; linePos++; }
                continue;
            }
            else if (c == '}' || c == ']')
            {
                prettyBuf[pos++] = '\r';
                prettyBuf[pos++] = '\n';
                linePos = 0;
                indent--;
                /*Add prefix + indent at new line*/
                for (int j = 0; j < indent; j++) { prettyBuf[pos++] = ' '; linePos++; }
                for (int k = 0; prefix[k] != '\0'; k++) { prettyBuf[pos++] = prefix[k]; linePos++; }
                prettyBuf[pos++] = c;
                linePos++;
                continue;
            }
            else if (c == ',')
            {
                prettyBuf[pos++] = c;
                prettyBuf[pos++] = '\r';
                prettyBuf[pos++] = '\n';
                linePos = 0;
                /*Add prefix + indent at new line*/
                for (int j = 0; j < indent; j++) { prettyBuf[pos++] = ' '; linePos++; }
                for (int k = 0; prefix[k] != '\0'; k++) { prettyBuf[pos++] = prefix[k]; linePos++; }
                continue;
            }

            /*Wrap line at max width*/
            if (linePos >= MAX_LINE_WIDTH)
            {
                prettyBuf[pos++] = '\r';
                prettyBuf[pos++] = '\n';
                linePos = 0;
                for (int k = 0; prefix[k] != '\0'; k++) { prettyBuf[pos++] = prefix[k]; linePos++; }
            }
        }

        /*Copy character*/
        prettyBuf[pos++] = c;
        linePos++;
    }

    /*End the last line with \r*/
    prettyBuf[pos++] = '\r';
    prettyBuf[pos++] = '\n';
    prettyBuf[pos] = '\0';

    Display_printf(display, 0, 0, "%s", prettyBuf);
}
void createTemplate(void)
{
    int16_t retVal;
    retVal = Json_createTemplate(&templateHandle, templateBuff.fileBuffer, templateBuff.fileSize);
    if(retVal < 0)
    {
        Display_printf(display, 0, 0, "Error: %d, Couldn't create template \n\r",retVal);
    }

    else
    {
        Display_printf(display, 0, 0, "Template object created successfully. \n\r");
    }
}

void createObject(void)
{
    int16_t retVal;
    char objSizeBuffer[OBJ_BUFFER_SIZE];

    objSize = 512;
    retVal = Json_createObject(&jsonObjHandle,templateHandle, objSize);

    if(retVal < 0)
    {
        Display_printf(display, 0, 0, "Error: %d, Couldn't create json object \n\r", retVal);
    }

    else
    {
        Display_printf(display, 0, 0, "Json object created successfully. \n\r");
    }
}

void parse(void)
{
    int16_t retVal;

    retVal = Json_parse(jsonObjHandle,jsonBuffer.fileBuffer, strlen(jsonBuffer.fileBuffer));
    if(retVal < 0)
    {
        Display_printf(display, 0, 0, "Error: %d, Couldn't parse the Json file \n\r", retVal);
    }

    else
    {
        Display_printf(display, 0, 0, "Json was parsed successfully \n\r");
    }
}

void getValue(char* pKey)
{
    int16_t retVal;
    int32_t numValue;
    uint16_t valueSize = 32;

    retVal = Json_getValue(jsonObjHandle,pKey,&numValue,&valueSize);
    if(retVal == JSON_RC__VALUE_IS_NULL)
    {
        Display_printf(display, 0, 0, "The value is null\n\r");
        return;
    }
    else if(retVal < 0)
    {
        Display_printf(display, 0, 0, "Error: %d, Couldn't get the data \n\r",retVal);
        return;
    }
    Display_printf(display, 0, 0, "The value is : %s \n\r", ((uint8_t)numValue == 0) ? "false" : "true");
}

int checkCreds(const char *filename)
{
    Json_Filename_t tempBuff;
    int16_t retVal;
    int32_t fileHandle;
    SlFsFileInfo_t fileInfo;

    /* Check if file exists and empty */
    if (sl_FsGetInfo((unsigned char *)filename, 0, &fileInfo) < 0) {
        Display_printf(display, 0, 0, "%s does not exist\n", filename);
        return 0;
    }

    if (fileInfo.Len == 0) {
        Display_printf(display, 0, 0, "%s is empty\n", filename);
        return 1;
    }

    Display_printf(display, 0, 0, "%s is being used\n", filename);

    /* Compare tempObj vs. jsonObjHandle */

    /* Return Codes:
     *  2 Credentials match
     *  1 File is empty
     *  0 File DNE
     * -1 Wrong credentials
     * -2 Wrong password
     * -3 File Reading Error*/

    /* Allocate Memory for buffer */
    tempBuff.fileBuffer = setTempBuffSize;

    /* Open File */
    fileHandle = sl_FsOpen((unsigned char *)filename, SL_FS_READ, 0);
    if (fileHandle < 0)
    {
        Display_printf(display, 0, 0, "Cannot open stored file\n");
        return -3;
    }

    /* Read File */
    retVal = sl_FsRead(fileHandle, 0, (unsigned char *)tempBuff.fileBuffer, fileInfo.Len);
    if (retVal < 0)
    {
        Display_printf(display, 0, 0, "Cannot read stored file\n");
        sl_FsClose(fileHandle, NULL, NULL, 0);
        return -3;
    }

    /* Close File */
    retVal = sl_FsClose(fileHandle, NULL, NULL, 0);
    if (retVal < 0)
    {
        Display_printf(display, 0, 0, "Cannot close stored file\n");
        return -3;
    }

    tempBuff.fileBuffer[fileInfo.Len] = '\0';

    /* Parse temp object */
    retVal = Json_parse(jsonObjHandle, tempBuff.fileBuffer, strlen(tempBuff.fileBuffer));
    if (retVal < 0)
    {
        Display_printf(display, 0, 0, "Cannot get parse new json file\n");
        return -3;
    }

    /* Get values from stored file */
    char userStored[32] = {0};
    char passStored[32] = {0};

    /* Buffers for username and password */
    uint16_t sizeUser = sizeof(userStored);
    uint16_t sizePass = sizeof(passStored);

    retVal = Json_getValue(jsonObjHandle, "user", userStored, &sizeUser);
    if (retVal < 0)
    {
        Display_printf(display, 0, 0, "Cannot get stored username\n");
        return -3;
    }

    retVal = Json_getValue(jsonObjHandle, "pword", passStored, &sizePass);
    if (retVal < 0)
    {
        Display_printf(display, 0, 0, "Cannot get stored password\n");
        return -3;
    }

    /* Get values from new object */
    char userNew[32] = {0};
    char passNew[32] = {0};
    sizeUser = sizeof(userNew);
    sizePass = sizeof(passNew);

    retVal = Json_getValue(jsonObjHandle, "user", userNew, &sizeUser);
    if (retVal < 0)
    {
        Display_printf(display, 0, 0, "Cannot get new username\n");
        return -3;
    }

    retVal = Json_getValue(jsonObjHandle, "pword", passNew, &sizePass);
    if (retVal < 0)
    {
        Display_printf(display, 0, 0, "Cannot get new password\n");
        return -3;
    }

    Display_printf(display, 0, 0, "%s: username = %s, password = %s\n",filename, userStored, passStored);
    Display_printf(display, 0, 0, "New File: username = %s, password = %s\n", userNew, passNew);

    /* Compare strings */

    if (strcmp(userStored, userNew) == 0)
    {
        if (strcmp(passStored, passNew) == 0)
        {
            Display_printf(display, 0, 0, "Username and Password matched: %s\n", filename);
            return 2;
        }
        else
        {
            Display_printf(display, 0, 0, "Wrong password for: %s in %s\n", userStored, filename);
            return -2;
        }
    }
    else
    {
        Display_printf(display, 0, 0, "No matching credentials for: %s \n", filename);
        return -1;
    }
}

void storeJson(char *jsonString, uint16_t size)
{
    int32_t fileHandle = 0;
    int32_t Status = 0;
    int8_t fileNum = -1;
    int8_t checkStatus;


    for(int i = 0; i < NUM_JSON_FILES; i++)
    {
        checkStatus = checkCreds(jsonFiles[i]);
        if(checkStatus == 2)
        {
            fileNum = i;
            break;
        }
        else if(checkStatus == -2){
            fileNum = -2;
        }
        else if((checkStatus == 0 || checkStatus == 1) && fileNum == -1)
        {
            fileNum = i;
        }
    }

    if(fileNum == -1)
    {
        Display_printf(display, 0, 0, "No available json files. Cannot create new account");
        return;
    }

    if(fileNum == -2)
    {
        Display_printf(display, 0, 0, "Cannot store duplicate usernames\n");
        return;
    }

    fileHandle = sl_FsOpen((unsigned char *)jsonFiles[fileNum],
                           SL_FS_CREATE | SL_FS_OVERWRITE | SL_FS_CREATE_MAX_SIZE(JSON_SIZE),
                           NULL);

    if(fileHandle < 0)
    {
        Display_printf(display, 0, 0, "Failed to open JSON file: %d\n", fileHandle);
        return;
    }

    Status = sl_FsWrite(fileHandle, 0, (unsigned char *)jsonString, size);
    if(Status < 0)
    {
        Display_printf(display, 0, 0, "Failed to write JSON file: %d\n", Status);
        sl_FsClose(fileHandle, NULL, NULL, 0);
        return;
    }

    Status = sl_FsClose(fileHandle, NULL, NULL, 0);

    if(Status < 0)
    {
        Display_printf(display, 0, 0, "Failed to close JSON file: %d\n", Status);
    }

    else
    {
        Display_printf(display, 0, 0, "JSON file saved successfully at %s.\n", jsonFiles[fileNum]);
    }
}

void build(void)
{
    char        *builtText;
    int16_t     retVal;
    uint16_t    builtTextSize;

    /* set object size to default size if zero was chosen */
    builtTextSize = (objSize == 0) ? JSON_DEFAULT_SIZE : objSize;

    /* creates buffer for building the json */
    builtText = (char *)setBuildSize;

    retVal = Json_build(jsonObjHandle,builtText,&builtTextSize);
    if(retVal < 0)
    {
        Display_printf(display, 0, 0, "Error: %d, Couldn't build the json.\n\r", retVal);
        return;
    }
    removeUnwantedChars(builtText);

    /*Pretty Format*/
    prettyDisplay(builtText);

    /*Store json File*/
    storeJson(builtText, builtTextSize);

    /*What the actual builtText holds*/
    Display_printf(display, 0, 0, "%s\n\r", builtText);
}

void destroyTemplate(void)
{
    int16_t retVal;

    retVal = Json_destroyTemplate(templateHandle);
    if(retVal < 0)
    {
       Display_printf(display, 0, 0, "Error: %d, Couldn't destroy the template.  \n\r", retVal);
       return;
    }
    Display_printf(display, 0, 0, "Template was destroyed successfully.  \n\r", retVal);
}

void destroyJsonObject(void)
{
    int16_t retVal;

    retVal = Json_destroyObject(jsonObjHandle);
    if(retVal < 0)
    {
        Display_printf(display, 0, 0, "Error: %d, Couldn't destroy the json.  \n\r", retVal);
        return;
    }
    Display_printf(display, 0, 0, "Json was destroyed successfully.  \n\r", retVal);
}

void storeTemplate(void)
{
    int32_t fileHandle;
    int32_t retVal;

    fileHandle = sl_FsOpen((unsigned char *)TEMPLATE_FILENAME,
                        SL_FS_CREATE | SL_FS_OVERWRITE | SL_FS_CREATE_MAX_SIZE(TEMPLATE_SIZE), NULL);
    if(fileHandle < 0)
    {
        Display_printf(display, 0, 0, "Failed to open template file: %d\n", fileHandle);
        return;
    }

    /*Write data to the file*/
    retVal = sl_FsWrite(fileHandle, 0, (unsigned char *)templateFile.fileBuffer, templateFile.fileSize);
    if(retVal < 0) {
        Display_printf(display, 0, 0, "Failed to write template file: %d\n", retVal);
    }

    /*Close the file*/
    retVal = sl_FsClose(fileHandle, NULL, NULL, 0);
    if(retVal < 0) {
        Display_printf(display, 0, 0, "Failed to close template file: %d\n", retVal);
    }

    Display_printf(display, 0, 0, "Template file written successfully.\n");
}

void* jsonThread(void *arg)
{
    sem_wait(&sem);
    int16_t status;

    status = sl_FsDel((unsigned char *)"json4", 0);
    status = sl_FsDel((unsigned char *)"json3", 0);
    status = sl_FsDel((unsigned char *)"json2", 0);
    status = sl_FsDel((unsigned char *)"json1", 0);

    jsonBuffer.fileBuffer = (char*)jsonString;
    int16_t retVal = 0;

    /*destroy any previous template handlers*/
    Json_destroyTemplate(templateHandle);

    templateBuff = templateFile;

    /*create template handler*/
    createTemplate();

    /*Store template in file system*/
    storeTemplate();

    /*readTemplate File*/
    templateSize = readFile(&templateBuff,TEMPLATE_FILENAME);

    createObject();

    parse();
    build();

    /* Destroy handlers */
    destroyJsonObject();
    destroyTemplate();

    sem_post(&sem);
    return NULL;
}
