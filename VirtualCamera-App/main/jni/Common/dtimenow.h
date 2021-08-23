#ifndef __SYSTEM_TIME_H__
#define __SYSTEM_TIME_H__

#include "common.h"

#ifndef WIN32
#include <sys/time.h>
#else
#include <windows.h>
CAPI int gettimeofday(struct timeval* tp, int* tz);
#endif

CAPI double dTimeNow_Sec();
CAPI double dTimeNow_MSec();
CAPI double dTimeNow_USec();

#endif