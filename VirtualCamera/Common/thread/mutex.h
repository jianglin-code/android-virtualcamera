#ifndef __SIMPLE_MUTEX_H__
#define __SIMPLE_MUTEX_H__

#include "Common/common.h"

typedef struct stMutex Mutex;

CAPI Mutex* Mutex_Create();
CAPI void Mutex_Destroy(Mutex *m);
CAPI void Mutex_Lock(Mutex *m);
CAPI void Mutex_Unlock(Mutex *m);


#endif