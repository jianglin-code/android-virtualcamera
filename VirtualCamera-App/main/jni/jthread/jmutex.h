
#ifndef JTHREAD_JMUTEX_H

#define JTHREAD_JMUTEX_H

#include <pthread.h>

#define ERR_JMUTEX_ALREADYINIT					-1
#define ERR_JMUTEX_NOTINIT						-2
#define ERR_JMUTEX_CANTCREATEMUTEX				-3

namespace jthread {

class JMutex {
public:
	JMutex();
	~JMutex();
	int Init();
	int Lock();
	int Unlock();
	bool IsInitialized() 						{ return initialized; }

private:
	pthread_mutex_t mutex;
	bool initialized;
};

} // end namespace

#endif // JTHREAD_JMUTEX_H

