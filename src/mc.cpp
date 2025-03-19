#include "mc.h"
#include "ddr_mem.h"
#include "dramsim_mem_ctrl.h"
#include "mem_ctrls.h"
#include "cache/nocache.h"
#include "cache/cacheonly.h"
#include "cache/alloy.h"
#include "cache/unison.h"
#include "cache/banshee.h"
#include "cache/ndc.h"
#include "zsim.h"

MemoryController::MemoryController(g_string& name, uint32_t frequency, uint32_t domain, Config& config)
    : _name(name), _cur_trace_len(0), _max_trace_len(10000) {
    // Initialize tracing
    _collect_trace = config.get<bool>("sys.mem.enableTrace", false);
    if (_collect_trace && _name == "mem-0") {
        _trace_dir = config.get<const char*>("sys.mem.traceDir", "./");
        FILE* f = fopen((_trace_dir + g_string("/") + _name + g_string("trace.bin")).c_str(), "wb");
        uint32_t num = 0;
        fwrite(&num, sizeof(uint32_t), 1, f);
        fclose(f);
        futex_init(&_lock);
    }

    g_string scheme = config.get<const char*>("sys.mem.cache_scheme", "NoCache");

    // Instantiate CacheScheme based on scheme type
    if (scheme == "AlloyCache") {
        _scheme = AlloyCache;
        _cache_scheme = new AlloyCacheScheme(config, this);
    } else if (scheme == "UnisonCache") {
        _scheme = UnisonCache;
        _cache_scheme = new UnisonCacheScheme(config, this);
    } else if (scheme == "BansheeCache") {
        _scheme = BansheeCache;
        _cache_scheme = new BansheeCacheScheme(config, this);
    } else if (scheme == "NoCache") {
        _scheme = NoCache;
        _cache_scheme = new NoCacheScheme(config, this);
    } else if (scheme == "CacheOnly") {
        _scheme = CacheOnly;
        _cache_scheme = new CacheOnlyScheme(config, this);
    } else if (scheme == "NDC") {
        _scheme = NDC;
        _cache_scheme = new NDCScheme(config, this);
    } else {
        panic("Invalid cache scheme: %s", scheme.c_str());
    }

    // Configure external DRAM
    _ext_type = config.get<const char*>("sys.mem.ext_dram.type", "Simple");
    g_string ext_dram_name = _name + g_string("-ext");
    if (_ext_type == "Simple") {
        uint32_t latency = config.get<uint32_t>("sys.mem.ext_dram.latency", 100);
        _ext_dram = new SimpleMemory(latency, ext_dram_name, config);
    } else if (_ext_type == "DDR") {
        _ext_dram = BuildDDRMemory(config, frequency, domain, ext_dram_name, 
                                   "sys.mem.ext_dram.", 4, 1.0);
    }

    // Configure MCDRAM if applicable
    if (_scheme != NoCache) {
        _mcdram_per_mc = config.get<uint32_t>("sys.mem.mcdram.mcdramPerMC", 4);
        _mcdram = new MemObject*[_mcdram_per_mc];
        _mcdram_type = config.get<const char*>("sys.mem.mcdram.type", "Simple");
        for (uint32_t i = 0; i < _mcdram_per_mc; i++) {
            g_string mcdram_name = _name + g_string("-mc-") + g_string(to_string(i).c_str());
            if (_mcdram_type == "Simple") {
                uint32_t latency = config.get<uint32_t>("sys.mem.mcdram.latency", 50);
                _mcdram[i] = new SimpleMemory(latency, mcdram_name, config);
            } else if (_mcdram_type == "DDR") {
                _mcdram[i] = BuildDDRMemory(config, frequency, domain, mcdram_name, 
                                            "sys.mem.mcdram.", 4, 1.0);
            }
        }
    }
}

MemoryController::~MemoryController() {
    delete _cache_scheme;
    if (_scheme != NoCache) {
        for (uint64_t i = 0; i < _cache_scheme->getNumSets(); i++)
            delete[] _cache_scheme->getSets()[i].ways;
        delete[] _cache_scheme->getSets();
        for (uint32_t i = 0; i < _mcdram_per_mc; i++) delete _mcdram[i];
        delete[] _mcdram;
    }
    delete _ext_dram;
}

uint64_t MemoryController::access(MemReq& req) {
    // Update request state
    switch (req.type) {
        case PUTS: case PUTX: *req.state = I; break;
        case GETS: *req.state = req.is(MemReq::NOEXCL) ? S : E; break;
        case GETX: *req.state = M; break;
        default: panic("Invalid request type");
    }
    if (req.type == PUTS) return req.cycle;

    futex_lock(&_lock);

    // Handle tracing if enabled
    if (_collect_trace && _name == "mem-0") {
        handleTraceCollection(req);
    }

    // Delegate access to CacheScheme
    uint64_t result = _cache_scheme->access(req);

    futex_unlock(&_lock);
    return result;
}

void MemoryController::handleTraceCollection(MemReq& req) {
    _address_trace[_cur_trace_len] = req.lineAddr;
    _type_trace[_cur_trace_len] = (req.type == PUTX) ? 1 : 0;
    _cur_trace_len++;
    if (_cur_trace_len == _max_trace_len) {
        FILE* f = fopen((_trace_dir + g_string("/") + _name + g_string("trace.bin")).c_str(), "ab");
        fwrite(_address_trace, sizeof(Address), _max_trace_len, f);
        fwrite(_type_trace, sizeof(uint32_t), _max_trace_len, f);
        fclose(f);
        _cur_trace_len = 0;
    }
}

void MemoryController::initStats(AggregateStat* parentStat) {
    AggregateStat* memStats = new AggregateStat();
    memStats->init(_name.c_str(), "Memory controller stats");
    _cache_scheme->initStats(memStats);
    _ext_dram->initStats(memStats);
    for (uint32_t i = 0; i < _mcdram_per_mc; i++) _mcdram[i]->initStats(memStats);
    parentStat->append(memStats);
}

DDRMemory* MemoryController::BuildDDRMemory(Config& config, uint32_t frequency,
                                            uint32_t domain, g_string name, const string& prefix, uint32_t tBL, double timing_scale) {
    uint32_t ranksPerChannel = config.get<uint32_t>(prefix + "ranksPerChannel", 4);
    uint32_t banksPerRank = config.get<uint32_t>(prefix + "banksPerRank", 8);                    // DDR3 std is 8
    uint32_t pageSize = config.get<uint32_t>(prefix + "pageSize", 8 * 1024);                     // 1Kb cols, x4 devices
    const char* tech = config.get<const char*>(prefix + "tech", "DDR3-1333-CL10");               // see cpp file for other techs
    const char* addrMapping = config.get<const char*>(prefix + "addrMapping", "rank:col:bank");  // address splitter interleaves channels; row always on top

    // If set, writes are deferred and bursted out to reduce WTR overheads
    bool deferWrites = config.get<bool>(prefix + "deferWrites", true);
    bool closedPage = config.get<bool>(prefix + "closedPage", true);

    // Max row hits before we stop prioritizing further row hits to this bank.
    // Balances throughput and fairness; 0 -> FCFS / high (e.g., -1) -> pure FR-FCFS
    uint32_t maxRowHits = config.get<uint32_t>(prefix + "maxRowHits", 4);

    // Request queues
    uint32_t queueDepth = config.get<uint32_t>(prefix + "queueDepth", 16);
    uint32_t controllerLatency = config.get<uint32_t>(prefix + "controllerLatency", 10);  // in system cycles

    auto mem = (DDRMemory*)gm_malloc(sizeof(DDRMemory));
    new (mem) DDRMemory(zinfo->lineSize, pageSize, ranksPerChannel, banksPerRank, frequency, tech, addrMapping, controllerLatency, queueDepth, maxRowHits, deferWrites, closedPage, domain, name, tBL, timing_scale);
    return mem;
}
