#ifndef __SIMPLE_THREAD_H__
#define __SIMPLE_THREAD_H__

#include "Common/common.h"

typedef void (*thread_func)(void *userdata);
typedef struct stThread Thread;

CAPI Thread* Thread_Create(thread_func func, void *userdata);
CAPI int Thread_Run(Thread *t);
CAPI void Thread_Join(Thread *t);
CAPI void Thread_Detach(Thread *t);
CAPI void Thread_Destroy(Thread *t);

#endif // __SIMPLE_THREAD_H__
