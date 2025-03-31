#ifndef _IdealFully_CACHE_SCHEME_H_
#define _IdealFully_CACHE_SCHEME_H_

#include <cmath>  // For log2
#include <string>
#include <vector>  // Add for LRU tracking

#include "cache/cache_scheme.h"
#include "mc.h"
#include "stats.h"

class IdealFullyScheme : public CacheScheme {
   private:
    // Statistics counters
    Counter _numCleanEviction;
    Counter _numDirtyEviction;
    Counter _numLoadHit;
    Counter _numLoadMiss;
    Counter _numStoreHit;
    Counter _numStoreMiss;

    uint32_t _num_cache_bits, _num_ext_bits, _num_shift_bits;
    uint64_t _num_line_entries;
    LineEntry* _line_entries;
    
    // LRU tracking - stores way indices in order from MRU (0) to LRU (n-1)
    std::vector<uint32_t> _lru_list;

    static const uint32_t MAX_ADDR_BITS = 58;  // 64 - 6 bits for cache line offset

   public:
    IdealFullyScheme(Config& config, MemoryController* mc) : CacheScheme(config, mc) {
        _scheme = IdealFully;
        assert(_mc->_mcdram_per_mc == 1);
        assert(_granularity == 64);
        assert(_num_sets == 1);
        _num_shift_bits = 6;

        _num_cache_bits = ceil(log2(_cache_size / 64));
        _num_ext_bits = ceil(log2(_ext_size / 64));
        _num_line_entries = _ext_size / 64;
        assert(_num_cache_bits <= _num_ext_bits);

        info("IdealFullyScheme initialized with %ld ways, %ld sets, %ld cache size, %ld ext size, %ld line entries", _num_ways, _num_sets, _cache_size, _ext_size, _num_line_entries);
        info("num_cache_bits = %ld, num_ext_bits = %ld, num_shift_bits = %ld", _num_cache_bits, _num_ext_bits, _num_shift_bits);

        _line_entries = (LineEntry*)gm_malloc(sizeof(LineEntry) * _num_line_entries);
        for (uint64_t i = 0; i < _num_line_entries; i++) {
            _line_entries[i].way = _num_ways;
        }
        
        // Initialize LRU list with all ways
        _lru_list.reserve(_num_ways);
        for (uint32_t i = 0; i < _num_ways; i++) {
            _lru_list.push_back(i);
        }
    }

    uint64_t access(MemReq& req) override;
    void period(MemReq& req) override;
    void initStats(AggregateStat* parentStat) override;
    
    // Helper method to update LRU state
    void updateLRU(uint32_t way);
    
    // Helper method to get the LRU way
    uint32_t getLRUWay();
};

#endif