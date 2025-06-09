#ifndef _IdealAssociative_CACHE_SCHEME_H_
#define _IdealAssociative_CACHE_SCHEME_H_

#include <cmath>  // For log2
#include <string>

#include "cache/cache_scheme.h"
#include "mc.h"
#include "stats.h"

class IdealAssociativeScheme : public CacheScheme {
   private:
    // Statistics counters
    Counter _numCleanEviction;
    Counter _numDirtyEviction;
    Counter _numLoadHit;
    Counter _numLoadMiss;
    Counter _numStoreHit;
    Counter _numStoreMiss;

    static const uint32_t MAX_ADDR_BITS = 58;  // 64 - 6 bits for cache line offset

   public:
    IdealAssociativeScheme(Config& config, MemoryController* mc) : CacheScheme(config, mc) {
        _scheme = IdealAssociative;
        assert(_mc->_mcdram_per_mc == 1);
        assert(_granularity == 64);

        info("IdealAssociativeScheme initialized with %llu ways, %llu sets, %llu cache size, %llu ext size", _num_ways, _num_sets, _cache_size, _ext_size);
    }

    uint64_t access(MemReq& req) override;
    void period(MemReq& req) override;
    void initStats(AggregateStat* parentStat) override;
};

#endif