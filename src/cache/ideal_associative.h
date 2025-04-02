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

    uint32_t _num_cache_bits, _num_ext_bits, _num_shift_bits;

    static const uint32_t MAX_ADDR_BITS = 58;  // 64 - 6 bits for cache line offset

   public:
    IdealAssociativeScheme(Config& config, MemoryController* mc) : CacheScheme(config, mc) {
        _scheme = IdealAssociative;
        assert(_mc->_mcdram_per_mc == 1);
        assert(_granularity == 64);
        _num_shift_bits = 6;

        _num_cache_bits = ceil(log2(_cache_size / 64));
        _num_ext_bits = ceil(log2(_ext_size / 64));
        assert(_num_cache_bits <= _num_ext_bits);

        info("IdealAssociativeScheme initialized with %llu ways, %llu sets, %llu cache size, %llu ext size", _num_ways, _num_sets, _cache_size, _ext_size);
        info("num_cache_bits = %ld, num_ext_bits = %ld, num_shift_bits = %ld", _num_cache_bits, _num_ext_bits, _num_shift_bits);
    }

    uint64_t access(MemReq& req) override;
    void period(MemReq& req) override;
    void initStats(AggregateStat* parentStat) override;
};

#endif