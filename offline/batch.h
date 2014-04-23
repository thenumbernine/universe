#ifndef BATCH_H
#define BATCH_H

#include <cassert>
#include <vector>
#include <list>
#include <pthread.h>

#include "exception.h"
#include "util.h"

class CritSec;

class Mutex {
	pthread_mutex_t m; 
	friend class CritSec;
public:
	Mutex() { pthread_mutex_init(&m, NULL); }
	~Mutex() { pthread_mutex_destroy(&m); }

	bool lock() { return !pthread_mutex_lock(&m); }
	bool unlock() { return !pthread_mutex_unlock(&m); }
	bool trylock() { return !pthread_mutex_trylock(&m); }
};

class CritSec {
	Mutex &m;
	bool success;
public:
	CritSec(Mutex &m_, bool try_ = false) 
	: 	m(m_),
		success(true)
	{ 
		if (try_) {
			trylock();
		} else {
			lock();
		}
	}
	~CritSec() { unlock(); }
	void lock() { success = m.lock(); }
	void unlock() { success = m.unlock(); }
	bool trylock() { success = m.trylock(); return success; }
	bool fail() { return !success; }
};

template<typename Worker>
class BatchProcessor {
protected:
	typedef typename Worker::ArgType ArgType;
	Mutex threadArgMutex, runningMutex;
	std::list<ArgType> threadArgs;
	std::vector<pthread_t> threads;
public:
	BatchProcessor() {
		threads.resize(1);
	}
	
	void setNumThreads(int numThreads) {
		CritSec runningCS(runningMutex, true);
		if (runningCS.fail()) throw Exception() << "can't modify while running";
		threads.resize(numThreads);
	}
	void addThreadArg(const ArgType &arg) {
		CritSec runningCS(runningMutex, true);
		if (runningCS.fail()) throw Exception() << "can't modify while running";
		threadArgs.push_back(arg);
	}

	//execute batch
	void operator()() {
		CritSec runningCS(runningMutex);

		for (int i = 0; i < threads.size(); i++) {
			int res = pthread_create(&threads[i], NULL, processThreadLoop, (void*)this);
			assert(res == 0);
		}

		for (int i = 0; i < threads.size(); i++) {
			void *status;
			int result = pthread_join(threads[i], &status);
			if (result) throw Exception() << "pthread_join failed";
		}
	}

	void runSingleThreaded() {
		Worker pf(this);
		for (typename std::list<ArgType>::iterator i = threadArgs.begin(); i != threadArgs.end(); ++i) {
			pf(*i);
		}
	}

	static void *processThreadLoop(void *batch_) {
		BatchProcessor<Worker> *batch = (BatchProcessor<Worker>*)batch_;
		return batch->threadLoop();
	}

	//individual thread loop:
	void *threadLoop() {
		Worker pf(this);
		for (;;) {
			CritSec threadArgCS(threadArgMutex);
			if (threadArgs.size() == 0) break;
			ArgType threadArg = threadArgs.front();
			threadArgs.pop_front();
			threadArgCS.unlock();
			
			profile(pf.desc(threadArg), pf, threadArg);
		}
		return NULL;
	}
};

#endif

