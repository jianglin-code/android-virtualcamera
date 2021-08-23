#include <stdlib.h>
#include <pthread.h>
#include <sched.h>

#include "../thread.h"

struct stThread {
    pthread_t t;
    thread_func func;
    void *userdata;
    pthread_attr_t attr;
};

static void* pthread_func(void *userdata) {
    RTPThread* thread = (RTPThread*)userdata;
    thread->func(thread->userdata);
    return NULL;
}

CAPI RTPThread* Thread_Create(thread_func func, void *userdata) {
    RTPThread *t = NULL;
    int ok = 0;
    
    do
    {
        t = (RTPThread *)malloc(sizeof(RTPThread));
        if (!t) break;
        
        t->t = 0;
        t->func = func;
        t->userdata = userdata;
        
        ok = 1;
    } while (0);
    
    if (!ok) {
        Thread_Destroy(t);
        t = NULL;
    }
    
    return t;
    
}

CAPI int Thread_Run(RTPThread *t) {
    int r = 0;
    if (t && t->t == 0) {
        struct sched_param param;
        pthread_attr_init(&(t->attr));
        int policy = SCHED_RR;
        pthread_attr_setschedpolicy(&(t->attr), policy);
        param.sched_priority = (sched_get_priority_max(policy) + sched_get_priority_min(policy))/3;
        pthread_attr_setschedparam(&(t->attr), &param);
        r = pthread_create(&(t->t), &(t->attr), pthread_func, t);
    }
    return r;
}

CAPI void Thread_Join(RTPThread *t) {
    if (t && t->t) {
        pthread_join(t->t, NULL);
        pthread_attr_destroy (&(t->attr));
        t->t = 0;
    }
}

CAPI void Thread_Destroy(RTPThread *t) {
    if (t) {
        Thread_Join(t);
        free(t);
    }
}
