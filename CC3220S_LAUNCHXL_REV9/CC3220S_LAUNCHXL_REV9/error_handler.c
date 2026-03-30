#include "json_make.h"

typedef enum {
    WatchdogError = -6,
    DispensingError = -5,
    OutOfPills  = -4,
    Disconnected = -3,
    ParsingError = -2,
    FatalParsing = -1,
    Connected = 0
}error_Code;

error_Code errorState;

void logError(int retCode)
{
    switch (retCode)
    {
    case (DispensingError):
        /**TODO: implement unjam needs compartment num*/
        break;
    case (OutOfPills):
        /**TODO: send status to web application*/
        break;
    case (Disconnected):
        /**TODO: display LCD icon */
        break;
    case (ParsingError):
        /**TODO: Request file to resend from web application in case of data corruption,
         * if multiple attpts fail, display error and send to web app, do not scan for error here*/
        break;
    case (FatalParsing):
        /**TODO: Request file to resend from web application in case of data corruption*/
        break;
    default:
        break;
        /*Print connected symbol top right*/
    }
}




