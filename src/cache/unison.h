#ifndef _UNISON_CACHE_SCHEME_H_
#define _UNISON_CACHE_SCHEME_H_

#include "cache/cache_utils.h"
#include "cache/cache_scheme.h"
#include "g_std/g_string.h"
#include "g_std/g_unordered_map.h"
#include "placement/page_placement.h"
#include "stats.h"

class UnisonCacheScheme : public CacheScheme {
   private:
    PagePlacementPolicy* _page_placement_policy;
    g_unordered_map<Address, TLBEntry> _tlb;
    uint32_t _footprint_size;
    Counter _numPlacement;
    Counter _numCleanEviction;
    Counter _numDirtyEviction;
    Counter _numLoadHit;
    Counter _numLoadMiss;
    Counter _numStoreHit;
    Counter _numStoreMiss;
    Counter _numTagLoad;
    Counter _numTagStore;
    Counter _numTouchedLines;
    Counter _numEvictedLines;
    Counter _numCounterAccess;

   public:
    UnisonCacheScheme(Config& config, MemoryController* mc)
        : CacheScheme(config, mc) {
        _scheme = UnisonCache;
        
        // Use gm_malloc for placement policy
        _page_placement_policy = (PagePlacementPolicy*)gm_malloc(sizeof(PagePlacementPolicy));
        new (_page_placement_policy) PagePlacementPolicy(this);
        _page_placement_policy->initialize(config);
        _footprint_size = config.get<uint32_t>("sys.mem.mcdram.footprint_size");
    }

    uint64_t access(MemReq& req) override;
    void period(MemReq& req) override;
    void initStats(AggregateStat* parentStat) override;
};

#endif