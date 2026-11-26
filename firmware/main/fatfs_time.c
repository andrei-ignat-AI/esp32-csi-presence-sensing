#include "ff.h"         
#include <sys/time.h>
#include <time.h>

DWORD get_fattime(void)
{
    time_t now;
    struct tm tm;
    time(&now);
    localtime_r(&now, &tm);

    DWORD fattime = ((DWORD)(tm.tm_year + 1900 - 1980) << 25)
                  | ((DWORD)(tm.tm_mon  + 1        ) << 21)
                  | ((DWORD)(tm.tm_mday       ) << 16)
                  | ((DWORD)(tm.tm_hour       ) << 11)
                  | ((DWORD)(tm.tm_min        ) <<  5)
                  | ((DWORD)(tm.tm_sec / 2    ) <<  0);
    return fattime;
}
