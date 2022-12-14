#ifndef FMUTEX_H_
#define FMUTEX_H_

#include "FreeRTOS.h"
#include "semphr.h"

class Fmutex {
public:
	Fmutex();
	virtual ~Fmutex();
	void lock();
	void unlock();
private:
	SemaphoreHandle_t mutex;
};

#endif /* FMUTEX_H_ */
