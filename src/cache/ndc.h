#ifndef _NDC_CACHE_SCHEME_H_
#define _NDC_CACHE_SCHEME_H_

#include "cache/cache_scheme.h"
#include "stats.h"

class NDCScheme : public CacheScheme {
private:
    // Statistics counters
    Counter _numCleanEviction;
    Counter _numDirtyEviction;
    Counter _numLoadHit;
    Counter _numLoadMiss;
    Counter _numStoreHit;
    Counter _numStoreMiss;

public:
    NDCScheme(Config& config, MemoryController* mc);
    uint64_t access(MemReq& req) override;
    void period(MemReq& req) override;
    void initStats(AggregateStat* parentStat) override;
};

#endif