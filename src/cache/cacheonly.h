#ifndef _CACHE_ONLY_SCHEME_H_
#define _CACHE_ONLY_SCHEME_H_

#include "cache/cache_scheme.h"
#include "stats.h"

class CacheOnlyScheme : public CacheScheme {
   private:
    Counter _numLoadHit;  // Counter for load hits
   public:
    CacheOnlyScheme(Config& config, MemoryController* mc)
        : CacheScheme(config, mc) {
        _scheme = CacheOnly;
    }
    uint64_t access(MemReq& req) override;
    void period(MemReq& req) override;
    void initStats(AggregateStat* parentStat) override;
};

#endif