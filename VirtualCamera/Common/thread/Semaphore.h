#ifndef __SEMAPHORE_H__
#define __SEMAPHORE_H__

#include "Common/common.h"

typedef struct stSemaphore Semaphore;

CAPI Semaphore* Semaphore_Create(const char *name, int value);
CAPI void Semaphore_Destroy(Semaphore *sem);

CAPI void Semaphore_Signal(Semaphore *sem);
CAPI void Semaphore_Wait(Semaphore *sem);

#endif // __SEMAPHORE_H__