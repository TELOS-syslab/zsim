#ifndef _BANSHEE_CACHE_SCHEME_H_
#define _BANSHEE_CACHE_SCHEME_H_

#include "cache_scheme.h"
#include "g_std/g_string.h"
#include "g_std/g_unordered_map.h"
#include "mc.h"  // For TagBuffer
#include "placement/page_placement.h"
#include "stats.h"

class BansheeCacheScheme : public CacheScheme {
   private:
    PagePlacementPolicy* _page_placement_policy;
    TagBuffer* _tag_buffer;
    g_unordered_map<Address, TLBEntry> _tlb;
    Counter _numPlacement;
    Counter _numCleanEviction;
    Counter _numDirtyEviction;
    Counter _numLoadHit;
    Counter _numLoadMiss;
    Counter _numStoreHit;
    Counter _numStoreMiss;
    Counter _numTagLoad;
    Counter _numTagStore;
    Counter _numTagBufferFlush;
    Counter _numTBDirtyHit;
    Counter _numTBDirtyMiss;
    Counter _numCounterAccess;

   public:
    BansheeCacheScheme(Config& config, MemoryController* mc)
        : CacheScheme(config, mc) {
        _scheme = BansheeCache;
        _page_placement_policy = (PagePlacementPolicy*)gm_malloc(sizeof(PagePlacementPolicy));
        new (_page_placement_policy) PagePlacementPolicy(this);
        _page_placement_policy->initialize(config);

        _tag_buffer = (TagBuffer*)gm_malloc(sizeof(TagBuffer));
        new (_tag_buffer) TagBuffer(config);
    }
    uint64_t access(MemReq& req) override;
    void period(MemReq& req) override;
    void initStats(AggregateStat* parentStat) override;

    TagBuffer* getTagBuffer() override { return _tag_buffer; }
};

#endif