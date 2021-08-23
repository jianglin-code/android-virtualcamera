
#ifndef JTHREAD_JMUTEXAUTOLOCK_H
#define JTHREAD_JMUTEXAUTOLOCK_H

#include "jmutex.h"

namespace jthread {

class JMutexAutoLock {
public:
	JMutexAutoLock(JMutex &m) : mutex(m)			{ mutex.Lock(); }
	~JMutexAutoLock()								{ mutex.Unlock(); }
private:
	JMutex &mutex;
};

} // end namespace

#endif // JTHREAD_JMUTEXAUTOLOCK_H

