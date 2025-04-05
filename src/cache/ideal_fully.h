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

    uint64_t _num_line_entries;
    LineEntry* _line_entries;
    
    // Array-based LRU tracking
    struct LRUEntry {
        uint32_t prev;  // Previous way in LRU order
        uint32_t next;  // Next way in LRU order
    };
    LRUEntry* _lru_array;  // Array of LRU entries indexed by way
    uint32_t _mru_way;     // Most recently used way
    uint32_t _lru_way;     // Least recently used way

    static const uint32_t MAX_ADDR_BITS = 58;  // 64 - 6 bits for cache line offset

   public:
    IdealFullyScheme(Config& config, MemoryController* mc) : CacheScheme(config, mc) {
        _scheme = IdealFully;
        assert(_mc->_mcdram_per_mc == 1);
        assert(_granularity == 64);
        assert(_num_sets == 1);

        _num_line_entries = _ext_size / 64;

        info("IdealFullyScheme initialized with %ld ways, %ld sets, %ld cache size, %ld ext size, %ld line entries", _num_ways, _num_sets, _cache_size, _ext_size, _num_line_entries);

        _line_entries = (LineEntry*)gm_malloc(sizeof(LineEntry) * _num_line_entries);
        for (uint64_t i = 0; i < _num_line_entries; i++) {
            _line_entries[i].way = _num_ways;
        }
        
        // Initialize LRU array
        _lru_array = new LRUEntry[_num_ways];
        for (uint32_t i = 0; i < _num_ways; i++) {
            _lru_array[i].prev = (i > 0) ? i-1 : _num_ways-1;
            _lru_array[i].next = (i < _num_ways-1) ? i+1 : 0;
        }
        _mru_way = 0;
        _lru_way = _num_ways-1;
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