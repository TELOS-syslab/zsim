#ifndef _COPY_CACHE_SCHEME_H_
#define _COPY_CACHE_SCHEME_H_

#include "cache_scheme.h"
#include "stats.h"

class CopyCacheScheme : public CacheScheme {
   private:
    Counter _numLoadHit;  // Counter for load hits
   public:
    CopyCacheScheme(Config& config, MemoryController* mc)
        : CacheScheme(config, mc) {
        _scheme = CopyCache;
    }
    uint64_t access(MemReq& req) override;
    void period(MemReq& req) override;
    void initStats(AggregateStat* parentStat) override;
};

#endif