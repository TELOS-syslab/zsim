#ifndef _IdealBalanced_CACHE_SCHEME_H_
#define _IdealBalanced_CACHE_SCHEME_H_

#include <cmath>  // For log2
#include <string>

#include "cache/cache_scheme.h"
#include "mc.h"
#include "stats.h"

class IdealBalancedScheme : public CacheScheme {
   private:
    // Statistics counters
    Counter _numCleanEviction;
    Counter _numDirtyEviction;
    Counter _numLoadHit;
    Counter _numLoadMiss;
    Counter _numStoreHit;
    Counter _numStoreMiss;

    uint64_t _ext_size;
    uint32_t _num_cache_bits, _num_ext_bits, _num_shift_bits;
    uint64_t _num_line_entries, _current_way;
    LineEntry* _line_entries;

    static const uint32_t MAX_ADDR_BITS = 58;  // 64 - 6 bits for cache line offset

   public:
    IdealBalancedScheme(Config& config, MemoryController* mc) : CacheScheme(config, mc) {
        _scheme = IdealBalanced;
        assert(_mc->_mcdram_per_mc == 1);
        assert(_granularity == 64);
        assert(_num_sets == 1);
        _num_shift_bits = 6;

        _ext_size = (uint64_t)config.get<uint32_t>("sys.mem.ext_dram.size", 128) * 1024 * 1024;
        _num_cache_bits = ceil(log2(_cache_size / _granularity));
        _num_ext_bits = ceil(log2(_ext_size / _granularity));
        _num_line_entries = _ext_size / _granularity;
        assert(_num_cache_bits <= _num_ext_bits);

        _line_entries = (LineEntry*)gm_malloc(sizeof(LineEntry) * _num_line_entries);
        for (uint64_t i = 0; i < _num_line_entries; i++) {
            _line_entries[i].way = _num_ways;
        }
        _current_way = 0;
        info("IdealBalancedScheme initialized with %ld ways, %ld sets, %ld cache size, %ld ext size, %ld line entries", _num_ways, _num_sets, _cache_size, _ext_size, _num_line_entries);
        info("num_cache_bits = %ld, num_ext_bits = %ld, num_shift_bits = %ld", _num_cache_bits, _num_ext_bits, _num_shift_bits);
    }

    uint64_t access(MemReq& req) override;
    void period(MemReq& req) override;
    void initStats(AggregateStat* parentStat) override;
};

#endif