#include <stdlib.h>
#include <semaphore.h>
#include <unistd.h>

#include "../Semaphore.h"

struct stSemaphore {
    sem_t sem_event;
    int init;
};

CAPI Semaphore* Semaphore_Create(const char *name, int value) {
    Semaphore *sem = NULL;
    int ok = 0;
    do
    {
        sem = (Semaphore *)malloc(sizeof(Semaphore));
        if (!sem) break;
        
        sem->init = 0;
        if (sem_init(&sem->sem_event, 0, 0) != 0)
            break;
        
        sem->init = 1;
        ok = 1;
    } while (0);
    
    if (!ok) {
        Semaphore_Destroy(sem);
        sem = NULL;
    }
    
    return sem;
}

CAPI void Semaphore_Destroy(Semaphore *sem) {
    if (sem) {
        if (sem->init)
            sem_destroy(&sem->sem_event);
        free(sem);
    }
}

CAPI void Semaphore_Signal(Semaphore *sem) {
    if (sem)
        sem_post(&sem->sem_event);
}

CAPI void Semaphore_Wait(Semaphore *sem) {
    if (sem)
        sem_wait(&sem->sem_event);
}
