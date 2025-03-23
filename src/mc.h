#ifndef _MC_H_
#define _MC_H_

#include "cache/cache_scheme.h"
#include "cache/cache_utils.h"
#include "config.h"
#include "g_std/g_string.h"
#include "g_std/g_unordered_map.h"
#include "memory_hierarchy.h"
#include "stats.h"
#include "galloc.h"

class DDRMemory;

class MemoryController : public MemObject {
   private:
    g_string _name;                 // Controller name
    lock_t _lock;                   // Lock for thread safety
    bool _collect_trace;            // Enable trace collection
    g_string _trace_dir;            // Directory for trace output
    Address _address_trace[10000];  // Address trace buffer
    uint32_t _type_trace[10000];    // Type trace buffer
    uint32_t _cur_trace_len;        // Current trace length
    uint32_t _max_trace_len;        // Maximum trace length
	
    uint64_t _num_requests;
    bool _bw_balance;    // Bandwidth balancing flag
    uint64_t _ds_index;  // Data structure index
    uint32_t _num_steps;
    uint64_t _step_length;

   public:
    MemObject* _ext_dram;     // External DRAM
    g_string _ext_type;       // External DRAM type
    MemObject** _mcdram;      // MCDRAM array
    uint32_t _mcdram_per_mc;  // MCDRAM instances per controller
    g_string _mcdram_type;    // MCDRAM type

    Scheme _scheme;              // Cache scheme type
    CacheScheme* _cache_scheme;  // Pointer to cache scheme implementation

    void handleTraceCollection(MemReq& req);  // Trace handling logic
    DDRMemory* BuildDDRMemory(Config& config, uint32_t freqMHz, uint32_t domain,
                              g_string name, const std::string& prefix, uint32_t tBL,
                              double timing_scale);  // DDR memory builder

   public:
    MemoryController(g_string& name, uint32_t freqMHz, uint32_t domain, Config& config);
    uint64_t access(MemReq& req) override;  // MemObject interface
    const char* getName() override { return _name.c_str(); }
    void initStats(AggregateStat* parentStat) override;

    // Accessors for CacheScheme and memory components
    Scheme getScheme() { return _scheme; };
    CacheScheme* getCacheScheme() { return _cache_scheme; }
};

#endif