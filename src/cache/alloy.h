#ifndef _ALLOY_CACHE_SCHEME_H_
#define _ALLOY_CACHE_SCHEME_H_

#include "cache_scheme.h"
#include "placement/line_placement.h"
#include "stats.h"

class AlloyCacheScheme : public CacheScheme {
   private:
    LinePlacementPolicy* _line_placement_policy;
    Counter _numPlacement;
    Counter _numCleanEviction;
    Counter _numDirtyEviction;
    Counter _numLoadHit;
    Counter _numLoadMiss;
    Counter _numStoreHit;
    Counter _numStoreMiss;
    Counter _numTagLoad;
    Counter _numTagStore;
    Counter _numCounterAccess;

   public:
    AlloyCacheScheme(Config& config, MemoryController* mc)
        : CacheScheme(config, mc) {
        _scheme = AlloyCache;
        
        // Use gm_malloc for placement policy
        _line_placement_policy = (LinePlacementPolicy*)gm_malloc(sizeof(LinePlacementPolicy));
        new (_line_placement_policy) LinePlacementPolicy();
        _line_placement_policy->initialize(config);
    }

    uint64_t access(MemReq& req) override;
    void period(MemReq& req) override;
    void initStats(AggregateStat* parentStat) override;
};

#endif