#pragma once

#include <cassert>
#include <vector>
#include <list>
#include <mutex>
#include <thread>
#include <memory>	//shared_ptr
#include "exception.h"
#include "util.h"

#if 0
class CritSec;

class Mutex {
	pthread_mutex_t m; 
	friend class CritSec;
public:
	Mutex() { pthread_mutex_init(&m, nullptr); }
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
#endif

template<typename Worker>
struct BatchProcessor {
protected:
	typedef typename Worker::ArgType ArgType;
	std::mutex threadArgMutex, runningMutex;
	std::list<ArgType> threadArgs;
	std::vector<std::shared_ptr<std::thread>> threads;
public:
	BatchProcessor() {
		threads.resize(std::thread::hardware_concurrency());
	}
	
	void setNumThreads(int numThreads) {
		std::unique_lock<std::mutex> runningCS(runningMutex, std::try_to_lock);
		if (!runningCS) throw Exception() << "can't modify while running";
		threads.resize(numThreads);
	}
	void addThreadArg(const ArgType &arg) {
		std::unique_lock<std::mutex> runningCS(runningMutex, std::try_to_lock);
		if (!runningCS) throw Exception() << "can't modify while running";
		threadArgs.push_back(arg);
	}

	//execute batch
	void operator()() {
		std::unique_lock<std::mutex> runningCS(runningMutex);

		for (int i = 0; i < threads.size(); i++) {
			threads[i] = std::make_shared<std::thread>([this]() {
				this->threadLoop();
			});
		}

		for (int i = 0; i < threads.size(); i++) {
			threads[i]->join();
		}
	}

	void runSingleThreaded() {
		Worker pf(this);
		for (const auto& i : threadArgs) {
			pf(i);
		}
	}

	//individual thread loop:
	void threadLoop() {
		Worker pf(this);
		for (;;) {
			std::unique_lock<std::mutex> threadArgCS(threadArgMutex);
			if (threadArgs.size() == 0) break;
			ArgType threadArg = threadArgs.front();
			threadArgs.pop_front();
			threadArgCS.unlock();
			
			profile(pf.desc(threadArg), [&](){
				try {
					pf(threadArg);
				} catch (std::exception &t) {
					std::cerr << "error: " << t.what() << std::endl;
				}
			});
		}
	}
};
