#ifndef _NO_CACHE_SCHEME_H_
#define _NO_CACHE_SCHEME_H_

#include "cache/cache_scheme.h"

class NoCacheScheme : public CacheScheme {
   private:
    Counter _numLoadHit;  // Scheme-specific counter
   public:
    NoCacheScheme(Config& config, MemoryController* mc)
        : CacheScheme(config, mc) {
        _scheme = NoCache;
    }
    uint64_t access(MemReq& req) override;
    void period(MemReq& req) override;
    void initStats(AggregateStat* parentStat) override;
};

#endif