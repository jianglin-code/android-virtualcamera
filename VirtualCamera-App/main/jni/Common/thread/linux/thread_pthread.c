#include <stdlib.h>
#include <pthread.h>

#include "../thread.h"

struct stThread {
    pthread_t t;
    thread_func func;
    void *userdata;
};

static void* pthread_func(void *userdata) {
    Thread* thread = (Thread*)userdata;
    thread->func(thread->userdata);
    return NULL;
}

CAPI Thread* Thread_Create(thread_func func, void *userdata) {
    Thread *t = NULL;
    int ok = 0;
    
    do
    {
        t = (Thread *)malloc(sizeof(Thread));
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

CAPI int Thread_Run(Thread *t) {
    int r = 0;
    if (t && t->t == 0) {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        r = pthread_create(&(t->t), &attr, pthread_func, t);
    }
    return r;
}

CAPI void Thread_Join(Thread *t) {
    if (t && t->t) {
        pthread_join(t->t, NULL);
        t->t = 0;
    }
}

CAPI void Thread_Destroy(Thread *t) {
    if (t) {
        Thread_Join(t);
        free(t);
    }
}
