
#include "jmutex.h"

namespace jthread
{

JMutex::JMutex()
{
	initialized = false;
}

JMutex::~JMutex()
{
	if (initialized)
		pthread_mutex_destroy(&mutex);
}

int JMutex::Init()
{
	if (initialized)
		return ERR_JMUTEX_ALREADYINIT;
	
	pthread_mutex_init(&mutex,NULL);
	initialized = true;
	return 0;	
}

int JMutex::Lock()
{
	if (!initialized)
		return ERR_JMUTEX_NOTINIT;
		
	pthread_mutex_lock(&mutex);
	return 0;
}

int JMutex::Unlock()
{
	if (!initialized)
		return ERR_JMUTEX_NOTINIT;
	
	pthread_mutex_unlock(&mutex);
	return 0;
}

} // end namespace

