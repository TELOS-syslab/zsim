#ifndef _IDEAL_HOTNESS_CACHE_SCHEME_H_
#define _IDEAL_HOTNESS_CACHE_SCHEME_H_

#include <cmath>
#include <string>
#include <vector>
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

    uint32_t _num_pages;       // Number of pages that can fit in cache
    uint32_t _lines_per_page;  // Number of cache lines per page
    uint32_t _period_counter;  // Counter for periodic migration checks
    static const uint32_t MIGRATION_PERIOD = 10000; // How often to check migrations
    
    PageEntry* _page_table;  // Array of page entries
    std::unordered_map<uint64_t, uint64_t> _page_location; // Maps page numbers to their location in cache
    
    // Helper methods
    uint64_t getPageNumber(uint64_t lineAddr);
    uint64_t getPageOffset(uint64_t lineAddr);
    uint32_t findVictimPage();
    void incrementFrequency(uint32_t pageIndex);
    void decayFrequencies();
    void migrateHotPages();

   public:
    IdealHotnessScheme(Config& config, MemoryController* mc);
    uint64_t access(MemReq& req) override;
    void period(MemReq& req) override;
    void initStats(AggregateStat* parentStat) override;
};

#endif
