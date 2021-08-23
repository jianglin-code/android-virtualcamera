#ifndef __SIMPLE_THREAD_H__
#define __SIMPLE_THREAD_H__

#include "Common/common.h"

typedef void (*thread_func)(void *userdata);
typedef struct stThread RTPThread;

CAPI RTPThread* Thread_Create(thread_func func, void *userdata);
CAPI int Thread_Run(RTPThread *t);
CAPI void Thread_Join(RTPThread *t);
CAPI void Thread_Detach(RTPThread *t);
CAPI void Thread_Destroy(RTPThread *t);

#endif // __SIMPLE_THREAD_H__
