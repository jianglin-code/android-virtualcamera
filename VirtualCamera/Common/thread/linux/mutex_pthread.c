#include <stdlib.h>
#include <pthread.h>
#include "../mutex.h"

struct stMutex {
    pthread_mutex_t m;
};

CAPI Mutex* Mutex_Create() {
    Mutex *m = (Mutex*)malloc(sizeof(Mutex));
    pthread_mutex_init(&m->m, NULL);
    return m;
}

CAPI void Mutex_Destroy(Mutex *m) {
    if (m) {
        free(m);
    }
}

CAPI void Mutex_Lock(Mutex *m) {
    if (m) {
        pthread_mutex_lock(&m->m);
    }
}

CAPI void Mutex_Unlock(Mutex *m) {
    if (m) {
        pthread_mutex_unlock(&m->m);
    }
}
