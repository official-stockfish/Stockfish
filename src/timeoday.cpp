/*
   (c) Copyright 1992 Eric Backus

   This software may be used freely so long as this copyright notice is
   left intact.  There is no warrantee on this software.
 */
#include <windows.h>
#include <time.h>
#include "dos.h"


int             gettimeofday(struct timeval * tp, struct timezone * tzp)
{
    SYSTEMTIME      systime;

    if (tp) {
        struct tm       tmrec;
        time_t          theTime = time(NULL);


        tmrec = *localtime(&theTime);
        tp->tv_sec = mktime(&tmrec);
        GetLocalTime(&systime); /* system time */

        tp->tv_usec = systime.wMilliseconds * 1000;
    }
    return 0;
}
