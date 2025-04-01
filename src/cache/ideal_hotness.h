#ifndef _IdealHotness_CACHE_SCHEME_H_
#define _IdealHotness_CACHE_SCHEME_H_

#include <cmath>  // For log2
#include <string>
#include <vector>  // Add for LRU tracking
#include <unordered_map>

#include "cache/cache_scheme.h"
#include "mc.h"
#include "stats.h"

class IdealHotnessScheme : public CacheScheme {
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
    
    // Array-based LRU tracking
    struct LRUEntry {
        uint32_t prev;  // Previous way in LRU order
        uint32_t next;  // Next way in LRU order
    };
    LRUEntry* _lru_array;  // Array of LRU entries indexed by way
    uint32_t _mru_way;     // Most recently used way
    uint32_t _lru_way;     // Least recently used way

    static const uint32_t MAX_ADDR_BITS = 58;  // 64 - 6 bits for cache line offset

    uint32_t _page_size;  // Page size in bytes
    uint32_t _num_pages;  // Number of pages that can fit in cache
    uint32_t _lines_per_page; // Number of cache lines per page
    
    // Track page access frequencies and mapping
    struct PageEntry {
        uint32_t frequency;  // Access frequency counter
        bool valid;
        bool dirty;
        uint64_t tag;       // Page tag
    };
    
    PageEntry* _page_table;  // Array of page entries
    std::unordered_map<uint64_t, uint32_t> _page_location; // Maps page numbers to their location in cache
    
    // Helper methods
    uint64_t getPageNumber(uint64_t lineAddr);
    uint64_t getPageOffset(uint64_t lineAddr);
    uint32_t findVictimPage();
    void incrementFrequency(uint32_t pageIndex);
    void decayFrequencies();

   public:
    IdealHotnessScheme(Config& config, MemoryController* mc) : CacheScheme(config, mc) {
        _scheme = IdealHotness;
    
        // Get page size from config (in bytes)
        _page_size = config.get<uint32_t>("sys.mem.mcdram.pageSize", 4096);
        
        // Calculate number of pages that can fit in cache
        _num_pages = _cache_size / _page_size;
        _lines_per_page = _page_size / _granularity;
        
        info("IdealHotnessScheme initialized with %d pages, page size %d bytes, %d lines per page",
            _num_pages, _page_size, _lines_per_page);
        
        // Initialize page table
        _page_table = new PageEntry[_num_pages];
        for (uint32_t i = 0; i < _num_pages; i++) {
            _page_table[i].frequency = 0;
            _page_table[i].valid = false;
            _page_table[i].dirty = false;
            _page_table[i].tag = 0;
        }

        assert(_mc->_mcdram_per_mc == 1);
        assert(_granularity == 64);
        assert(_num_sets == 1);
        _num_shift_bits = 6;

        _num_cache_bits = ceil(log2(_cache_size / 64));
        _num_ext_bits = ceil(log2(_ext_size / 64));
        _num_line_entries = _ext_size / 64;
        assert(_num_cache_bits <= _num_ext_bits);

        info("IdealHotnessScheme initialized with %ld ways, %ld sets, %ld cache size, %ld ext size, %ld line entries", _num_ways, _num_sets, _cache_size, _ext_size, _num_line_entries);
        info("num_cache_bits = %ld, num_ext_bits = %ld, num_shift_bits = %ld", _num_cache_bits, _num_ext_bits, _num_shift_bits);

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

        _page_size = 64;  // Assuming a default page size of 64 bytes
        _num_pages = _cache_size / _page_size;
        _lines_per_page = _page_size / 64;

        _page_table = new PageEntry[_num_pages];
        for (uint32_t i = 0; i < _num_pages; i++) {
            _page_table[i].frequency = 0;
            _page_table[i].valid = false;
            _page_table[i].dirty = false;
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