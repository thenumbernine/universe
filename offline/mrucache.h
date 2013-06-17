#ifndef MRUCACHE_H
#define MRUCACHE_H

#include <assert.h>
#include <list>
#include "exception.h"

/*
MRUCache - keeps N files open
used when reading/writing to a multiple-file dataset
when a file is used, the queue is checked
	then the file ptr is put on the front of the queu
much much better performance than opening/closing files for each read/write

Entry = the internal structure that holds the type info
	this is created/deleted when a new object is added/removed to the queue 
Entry must have a member type defined: 
	ArgType 			= the type that we are storing information associated with
	Entry(ArgType *)	= constructor for the entry
	ArgType *getArg()	= getter for the stored arg 
	
*/
template<typename Entry_>
struct MRUCache {
	typedef Entry_ Entry;
	typedef typename Entry::ArgType ArgType;
	std::list<Entry*> queue;
	int maxEntries;
	
	MRUCache(int maxEntries_) : maxEntries(maxEntries_) {}
	
	/*
	not mt safe
	don't hold an Entry pointer past another getEntry() call, or it may become invalidated
	*/
	Entry *getEntry(ArgType *obj) {
		for (typename std::list<Entry*>::iterator i = queue.begin(); i != queue.end(); ++i) {
			Entry_ *e = *i;
			if (e->getArg() == obj) {
				if (i != queue.begin()) {
					queue.erase(i);
					queue.push_front(e);	//priority based -- front is best
				}
				return e;
			}
		}
		Entry_ *fe = new Entry_(obj);
		queue.push_front(fe);
		if (queue.size() > maxEntries) {
			Entry_ *e = *queue.rbegin();
			fclose(e->fp);
			queue.pop_back();
		}
		return fe;	
	}

	void removeEntry(ArgType *obj) {
		for (typename std::list<Entry*>::iterator i = queue.begin(); i != queue.end(); ++i) {
			Entry *e = *i;
			if (e->getArg() == obj) {
				queue.erase(i);
				delete e;
				return;
			}
		}
		throw Exception() << "tried to remove an object that wasn't in the priority queue"; 
		assert(false);
	}
};

#endif

