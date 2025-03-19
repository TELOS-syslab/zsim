#pragma once
#include "memory_hierarchy.h"
#include "cache/cache_utils.h"
#include "cache/cache_scheme.h"

class DramCache;

class OSPlacementPolicy
{
public:
	OSPlacementPolicy(CacheScheme * cache_scheme) : _cache_scheme(cache_scheme) {};
	void handleCacheAccess(Address tag, ReqType type);
	uint64_t remapPages(); 
	
	void clearStats(); 
	//void printInfo();
	 
private:
	
	CacheScheme * _cache_scheme;
};
