#include <iostream>
#include <algorithm>
#include <list>
#include <map>

template<class K, class V>
class MRUCache
{
private:
 typedef std::list<K> MRU;
 typedef std::map<K,V> Cache;
 typedef V (*Constructor)(K);
 typedef void (*Destructor)(V);

 Constructor ctor;
 Destructor dtor;
 size_t cacheSize;
 MRU mru;
 Cache cache;


public:
 MRUCache(Constructor ctor, Destructor dtor, size_t cacheSize)
  : ctor(ctor),
    cacheSize(cacheSize)
 {
 }

 V get(K k)
 {
   typename Cache::iterator cIt = cache.find(k);
   if(cIt != cache.end())
   {
     V cached = cIt->second;
     mru.erase(std::find(mru.begin(), mru.end(), k), mru.end());
     mru.push_front(k);
     return cached;
  }
   else
   {
     if(mru.size() >= cacheSize)
     {
       K oldest = mru.back();
       dtor(cache[oldest]);
	   mru.pop_back();
	   cache.erase(oldest);
     }

     V v = ctor(k);
     mru.push_front(k);
     cache[k] = v;
     return v;
   }
 }

 void remove(K k)
 {
	dtor(cache[k]);
	cache.erase(k);
	mru.erase(std::find(mru.begin(), mru.end(), k), mru.end());
 }  
};

