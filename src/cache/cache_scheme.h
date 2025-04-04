#ifndef _CACHE_SCHEME_H_
#define _CACHE_SCHEME_H_

#include <cmath>
#include "cache/cache_utils.h"
#include "memory_hierarchy.h"
#include "stats.h"
#include "galloc.h"  // Add this for gm_malloc
#include "zsim.h"

#define MAX_STEPS 10000
class MemoryController;
class TagBuffer;

class CacheScheme {
   protected:
    Scheme _scheme;         // Cache scheme type
    MemoryController* _mc;  // Pointer to access MemoryController components
    uint64_t _granularity;  // Cache line size
    uint64_t _num_ways;     // Associativity
    uint64_t _cache_size;   // Total cache size in bytes
    uint64_t _ext_size;     // Total external memory size in bytes
    uint64_t _page_size;
    uint32_t _page_bits;
    uint32_t _cache_bits;
    uint32_t _ext_bits;
    uint32_t _shift_bits;

    uint64_t _num_sets;     // Number of sets
    Set* _cache;            // Cache structure
    bool _sram_tag;         // SRAM tag flag
    uint32_t _llc_latency;  // Last-level cache latency
    bool _bw_balance;       // Bandwidth balancing flag
    uint64_t _ds_index;     // Data structure index

    // Common counters for statistics
    uint64_t _num_requests;
    uint64_t _num_hit_per_step;
    uint64_t _num_miss_per_step;
    uint64_t _mc_bw_per_step;
    uint64_t _ext_bw_per_step;
    double _miss_rate_trace[MAX_STEPS];
    

   public:
    CacheScheme(Config& config, MemoryController* mc)
        : _mc(mc) {
        // Cache configuration
        _scheme = UNKNOWN;
        _sram_tag = config.get<bool>("sys.mem.sram_tag", false);
        _llc_latency = config.get<uint32_t>("sys.caches.l3.latency");
        _bw_balance = config.get<bool>("sys.mem.bwBalance", false);
        _ds_index = 0;
        _shift_bits = 6;
        
        _granularity = config.get<uint32_t>("sys.mem.mcdram.cache_granularity", 64);
        _num_ways = config.get<uint32_t>("sys.mem.mcdram.num_ways", 1);
        _page_size = config.get<uint32_t>("sys.mem.page_size", 4096); // 4096, 2097152
        _cache_size = (uint64_t)config.get<uint32_t>("sys.mem.mcdram.size", 128) * 1024 * 1024; // Bytes
        _ext_size = (uint64_t)config.get<uint32_t>("sys.mem.ext_dram.size", 0) * 1024 * 1024; // Bytes
        _page_bits = log2(_page_size);
        if (_page_bits < 6) {
            panic("Page size %d is too small, must be at least 64 bytes", _page_size);
        } else if (_page_bits > 12) {
            panic("Page size %d is too large, must be at most 4096 bytes", _page_size);
        }
        if (_cache_size == 0) {
            _cache_size = _page_size; // at least a page
        }
        assert(_cache_size % _page_size == 0);
        _cache_bits = log2(_cache_size);
        if (_ext_size == 0) {
            _ext_size = 0xFFFFFFFFFFFFFFFF;
        }
        _ext_bits = log2(_ext_size);
        if (_num_ways == 0) {
            _num_ways = _cache_size / _granularity;
        }
        _num_sets = _cache_size / _num_ways / _granularity;

        info("cache_size = %ld, num_ways = %ld, num_sets = %ld, granularity = %ld", _cache_size, _num_ways, _num_sets, _granularity);

        // _cache = new Set[_num_sets];
        _cache = (Set*)gm_malloc(sizeof(Set) * _num_sets);
        for (uint64_t i = 0; i < _num_sets; i++) {
            // _cache[i].ways = new Way[_num_ways];
            _cache[i].ways = (Way*)gm_malloc(sizeof(Way) * _num_ways);
            _cache[i].num_ways = _num_ways;
            for (uint32_t j = 0; j < _num_ways; j++) {
                _cache[i].ways[j].valid = false;
                _cache[i].ways[j].dirty = false;
                _cache[i].ways[j].tag = 0;
            }
        }

        // Stats initialization
        _num_hit_per_step = 0;
        _num_miss_per_step = 0;
        _mc_bw_per_step = 0;
        _ext_bw_per_step = 0;
        for (uint32_t i = 0; i < MAX_STEPS; i++)
            _miss_rate_trace[i] = 0;
        _num_requests = 0;
    }

    virtual uint64_t access(MemReq& req) = 0;  // Pure virtual method for cache access
    virtual void period(MemReq& req) = 0;
    virtual void initStats(AggregateStat* parentStat) = 0;  // Stats initialization

    virtual TagBuffer* getTagBuffer() { return nullptr; }
    uint64_t getNumRequests() { return _num_requests; };
    uint64_t getNumSets() { return _num_sets; };
    uint32_t getNumWays() { return _num_ways; };
    double getRecentMissRate() { return (double)_num_miss_per_step / (_num_miss_per_step + _num_hit_per_step); };
    Set* getSets() { return _cache; };
    uint64_t getGranularity() const { return _granularity; }
    Scheme getScheme() { return _scheme; };
};

#endif