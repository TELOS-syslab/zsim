#include "mc.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "cache/alloy.h"
#include "cache/banshee.h"
#include "cache/cacheonly.h"
#include "cache/chamo.h"
#include "cache/copycache.h"
#include "cache/ideal_associative.h"
#include "cache/ideal_balanced.h"
#include "cache/ideal_fully.h"
#include "cache/ndc.h"
#include "cache/nocache.h"
#include "cache/unison.h"
#include "ddr_mem.h"
#include "dramsim3_mem_ctrl.h"
#include "dramsim_mem_ctrl.h"
#include "mem_ctrls.h"
#include "zsim.h"

// Helper function to check if a directory exists
inline bool file_exists(const std::string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0;
}

MemoryController::MemoryController(g_string& name, uint32_t freqMHz, uint32_t domain, Config& config, std::string suffix_str)
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

    _bw_balance = config.get<bool>("sys.mem.bwBalance", false);
    g_string scheme = config.get<const char*>("sys.mem.cache_scheme", "NoCache");

    // Configure external DRAM
    _ext_type = config.get<const char*>("sys.mem.ext_dram.type", "Simple");
    double timing_scale = config.get<double>("sys.mem.dram_timing_scale", 1);
    g_string ext_dram_name = _name + g_string("-ext");
    if (_ext_type == "Simple") {
        uint32_t latency = config.get<uint32_t>("sys.mem.ext_dram.latency", 100);
        _ext_dram = (SimpleMemory*)gm_malloc(sizeof(SimpleMemory));
        new (_ext_dram) SimpleMemory(latency, ext_dram_name, config);
    } else if (_ext_type == "DDR")
        _ext_dram = BuildDDRMemory(config, freqMHz, domain, ext_dram_name, "sys.mem.ext_dram.", 4, 1.0);
    else if (_ext_type == "MD1") {
        uint32_t latency = config.get<uint32_t>("sys.mem.ext_dram.latency", 100);
        uint32_t bandwidth = config.get<uint32_t>("sys.mem.ext_dram.bandwidth", 6400);
        _ext_dram = (MD1Memory*)gm_malloc(sizeof(MD1Memory));
        new (_ext_dram) MD1Memory(64, freqMHz, bandwidth, latency, ext_dram_name);
    } else if (_ext_type == "DRAMSim") {
        uint64_t cpuFreqHz = 1000000 * freqMHz;
        uint32_t capacity = config.get<uint32_t>("sys.mem.ext_dram.capacityMB", 16384);
        string dramTechIni = config.get<const char*>("sys.mem.ext_dram.techIni");
        string dramSystemIni = config.get<const char*>("sys.mem.ext_dram.systemIni");
        string outputDir = config.get<const char*>("sys.mem.ext_dram.outputDir");
        outputDir = outputDir + "/" + suffix_str;
        if (!file_exists(outputDir)) {
            if (mkdir(outputDir.c_str(), 0777) != 0) {
                panic("Could not create directory %s: %s", outputDir.c_str(), strerror(errno));
            }
        }
        string traceName = config.get<const char*>("sys.mem.ext_dram.traceName", "dramsim");
        traceName += "_ext";
        _ext_dram = (DRAMSimMemory*)gm_malloc(sizeof(DRAMSimMemory));
        uint32_t latency = config.get<uint32_t>("sys.mem.ext_dram.latency", 100);
        new (_ext_dram) DRAMSimMemory(dramTechIni, dramSystemIni, outputDir, traceName, capacity, cpuFreqHz, latency, domain, ext_dram_name);
    } else if (_ext_type == "DRAMSim3") {
        int cpuFreqMHz = freqMHz;
        string dramIni = config.get<const char*>("sys.mem.ext_dram.configIni");
        string outputDir = config.get<const char*>("sys.mem.ext_dram.outputDir");
        outputDir = outputDir + "/" + suffix_str;
        if (!file_exists(outputDir)) {
            if (mkdir(outputDir.c_str(), 0777) != 0) {
                panic("Could not create directory %s: %s", outputDir.c_str(), strerror(errno));
            }
        }
        uint32_t latency = config.get<uint32_t>("sys.mem.ext_dram.latency", 100);
        info("Initializing DRAMSim3 with config %s, output dir %s, freq %d MHz",
             dramIni.c_str(), outputDir.c_str(), cpuFreqMHz);
        _ext_dram = (DRAMSim3Memory*)gm_malloc(sizeof(DRAMSim3Memory));
        new (_ext_dram) DRAMSim3Memory(dramIni, outputDir, cpuFreqMHz, latency, domain, ext_dram_name);
    } else
        panic("Invalid memory controller type %s", _ext_type.c_str());

    // Configure MCDRAM if applicable
    if (_scheme != NoCache) {
        // Configure the MC-Dram (Timing Model)
        _mcdram_per_mc = config.get<uint32_t>("sys.mem.mcdram.mcdramPerMC", 4);
        // _mcdram = new MemObject * [_mcdram_per_mc];
        _mcdram = (MemObject**)gm_malloc(sizeof(MemObject*) * _mcdram_per_mc);
        _mcdram_type = config.get<const char*>("sys.mem.mcdram.type", "Simple");
        for (uint32_t i = 0; i < _mcdram_per_mc; i++) {
            g_string mcdram_name = _name + g_string("-mc-") + g_string(to_string(i).c_str());
            // g_string mcdram_name(ss.str().c_str());
            if (_mcdram_type == "Simple") {
                uint32_t latency = config.get<uint32_t>("sys.mem.mcdram.latency", 50);
                _mcdram[i] = (SimpleMemory*)gm_malloc(sizeof(SimpleMemory));
                new (_mcdram[i]) SimpleMemory(latency, mcdram_name, config);
                //_mcdram[i] = new SimpleMemory(latency, mcdram_name, config);
            } else if (_mcdram_type == "DDR") {
                // XXX HACK tBL for mcdram is 1, so for data access, should multiply by 2, for tad access, should multiply by 3.
                _mcdram[i] = BuildDDRMemory(config, freqMHz, domain, mcdram_name, "sys.mem.mcdram.", 4, timing_scale);
            } else if (_mcdram_type == "MD1") {
                uint32_t latency = config.get<uint32_t>("sys.mem.mcdram.latency", 50);
                uint32_t bandwidth = config.get<uint32_t>("sys.mem.mcdram.bandwidth", 12800);
                _mcdram[i] = (MD1Memory*)gm_malloc(sizeof(MD1Memory));
                new (_mcdram[i]) MD1Memory(64, freqMHz, bandwidth, latency, mcdram_name);
            } else if (_mcdram_type == "DRAMSim") {
                uint64_t cpuFreqHz = 1000000 * freqMHz;
                uint32_t capacity = config.get<uint32_t>("sys.mem.mcdram.capacityMB", 16384);
                string dramTechIni = config.get<const char*>("sys.mem.mcdram.techIni");
                string dramSystemIni = config.get<const char*>("sys.mem.mcdram.systemIni");
                string outputDir = config.get<const char*>("sys.mem.mcdram.outputDir");
                outputDir = outputDir + "/" + suffix_str;
                if (!file_exists(outputDir)) {
                    if (mkdir(outputDir.c_str(), 0777) != 0) {
                        panic("Could not create directory %s: %s", outputDir.c_str(), strerror(errno));
                    }
                }
                string traceName = config.get<const char*>("sys.mem.mcdram.traceName");
                traceName += "_mc";
                traceName += to_string(i);
                _mcdram[i] = (DRAMSimMemory*)gm_malloc(sizeof(DRAMSimMemory));
                uint32_t latency = config.get<uint32_t>("sys.mem.mcdram.latency", 50);
                new (_mcdram[i]) DRAMSimMemory(dramTechIni, dramSystemIni, outputDir, traceName, capacity, cpuFreqHz, latency, domain, mcdram_name);
            } else if (_mcdram_type == "DRAMSim3") {
                int cpuFreqMHz = freqMHz;
                string dramIni = config.get<const char*>("sys.mem.mcdram.configIni");
                string outputDir = config.get<const char*>("sys.mem.mcdram.outputDir");
                outputDir = outputDir + "/" + suffix_str;
                if (!file_exists(outputDir)) {
                    if (mkdir(outputDir.c_str(), 0777) != 0) {
                        panic("Could not create directory %s: %s", outputDir.c_str(), strerror(errno));
                    }
                }
                uint32_t latency = config.get<uint32_t>("sys.mem.mcdram.latency", 0);
                _mcdram[i] = (DRAMSim3Memory*)gm_malloc(sizeof(DRAMSim3Memory));
                info("Initializing DRAMSim3 with config %s, output dir %s, freq %d MHz",
                     dramIni.c_str(), outputDir.c_str(), cpuFreqMHz);
                new (_mcdram[i]) DRAMSim3Memory(dramIni, outputDir, cpuFreqMHz, latency, domain, mcdram_name);
            } else
                panic("Invalid memory controller type %s", _mcdram_type.c_str());
        }
    }

    g_string placement_scheme = config.get<const char*>("sys.mem.mcdram.placementPolicy", "LRU");

    // Instantiate CacheScheme based on scheme type
    if (scheme == "AlloyCache") {
        _scheme = AlloyCache;
        _cache_scheme = new (gm_malloc(sizeof(AlloyCacheScheme))) AlloyCacheScheme(config, this);
    } else if (scheme == "UnisonCache") {
        _scheme = UnisonCache;
        _cache_scheme = new (gm_malloc(sizeof(UnisonCacheScheme))) UnisonCacheScheme(config, this);
    } else if (scheme == "BansheeCache") {
        _scheme = BansheeCache;
        _cache_scheme = new (gm_malloc(sizeof(BansheeCacheScheme))) BansheeCacheScheme(config, this);
    } else if (scheme == "NoCache") {
        _scheme = NoCache;
        _cache_scheme = new (gm_malloc(sizeof(NoCacheScheme))) NoCacheScheme(config, this);
    } else if (scheme == "CacheOnly") {
        _scheme = CacheOnly;
        _cache_scheme = new (gm_malloc(sizeof(CacheOnlyScheme))) CacheOnlyScheme(config, this);
    } else if (scheme == "CopyCache") {
        _scheme = CopyCache;
        _cache_scheme = new (gm_malloc(sizeof(CopyCacheScheme))) CopyCacheScheme(config, this);
    } else if (scheme == "NDC") {
        _scheme = NDC;
        _cache_scheme = new (gm_malloc(sizeof(NDCScheme))) NDCScheme(config, this);
    } else if (scheme == "IdealBalanced") {
        _scheme = IdealBalanced;
        _cache_scheme = new (gm_malloc(sizeof(IdealBalancedScheme))) IdealBalancedScheme(config, this);
    } else if (scheme == "IdealAssociative") {
        _scheme = IdealAssociative;
        _cache_scheme = new (gm_malloc(sizeof(IdealAssociativeScheme))) IdealAssociativeScheme(config, this);
    } else if (scheme == "IdealFully") {
        _scheme = IdealFully;
        _cache_scheme = new (gm_malloc(sizeof(IdealFullyScheme))) IdealFullyScheme(config, this);
    } else if (scheme == "CHAMO") {
        _scheme = CHAMO;
        _cache_scheme = new (gm_malloc(sizeof(CHAMOScheme))) CHAMOScheme(config, this);
    } else {
        panic("Invalid cache scheme %s", scheme.c_str());
    }

    _num_steps = 0;
    uint64_t cache_size = (uint64_t)config.get<uint32_t>("sys.mem.mcdram.size", 128) * 1024 * 1024;
    uint64_t ext_size = (uint64_t)config.get<uint32_t>("sys.mem.ext_dram.size", 0) * 1024 * 1024;
    if (cache_size == 0) {
        cache_size = 1;
    }
    _cache_bits = log2(cache_size);
    if (ext_size == 0) {
        ext_size = 0xFFFFFFFFFFFFFFFF;
    }
    _ext_bits = log2(ext_size);
    _step_length = cache_size / 64 / 10;
    info("cache_size: %lu, step_length: %lu", cache_size, _step_length);
    _num_requests = 0;

    
    uint32_t _page_size = config.get<uint32_t>("sys.mem.page_size", 4096); // 4096, 2097152
    _page_bits = log2(_page_size);
    if (_page_bits < 6) {
        panic("Page size %d is too small, must be at least 64 bytes", _page_size);
    } else if (_page_bits > 12) {
        panic("Page size %d is too large, must be at most 4096 bytes", _page_size);
    }

    _page_map_scheme = config.get<const char*>("sys.mem.pagemap_scheme", "Identical"); // Identical, Random, Johnny
    if (_page_map_scheme == "Johnny") {
        _johnny_ptr = 0;
    } else if (_page_map_scheme == "Random") {
        srand48_r((uint64_t)this, &_buffer);
    } else if (_page_map_scheme == "Identical") { 
        // Do nothing
    } else {
        panic("Invalid page mapping scheme %s", _page_map_scheme.c_str());
    }

    info("MemoryController %s initialized with page size %d, page mapping scheme %s", _name.c_str(), _page_size, _page_map_scheme.c_str());
    info("MemoryController %s initialized with cache size %lu, ext size %lu", _name.c_str(), cache_size, ext_size);

}

Address MemoryController::mapPage(Address vLineAddr) {
    Address pLineAddr;
    if (_page_map_scheme == "Identical") {
        pLineAddr = vLineAddr & ((1UL << (_ext_bits - 6)) - 1);
        return pLineAddr;
    }

    Address vpgnum = vLineAddr >> (_page_bits - 6); 
    uint64_t pgnum;
    // info("Johnny page mapping: vLineAddr = %lx, vpgnum = %lx", vLineAddr, vpgnum);
    if (_tlb.find(vpgnum) == _tlb.end()) {
        if (_page_map_scheme == "Johnny") {
            pgnum = _johnny_ptr;
            _johnny_ptr++;
            _johnny_ptr &= ((1UL << (_ext_bits - 6)) - 1);
            // info("Johnny page mapping: _johnny_ptr = %lx", _johnny_ptr);
        } else if (_page_map_scheme == "Random") {
            do {
                int64_t rand;
                lrand48_r(&_buffer, &rand);
                // pgnum = rand & 0x000fffffffffffff;
                // info("Random page mapping: pgnum = %lx => %lx", rand, rand & ((1UL << (_ext_bits - 6)) - 1));
                // N.B. TAKE CARE OF '<<'
                pgnum = rand & ((1UL << (_ext_bits - 6)) - 1);   
            } while (_exist_pgnum.find(pgnum) != _exist_pgnum.end());
        } else {
            panic("Invalid page mapping scheme %s", _page_map_scheme.c_str());
        }
        _tlb[vpgnum] = pgnum;
        _exist_pgnum.insert(pgnum);
    } else 
        pgnum = _tlb[vpgnum];
    pLineAddr = (pgnum << (_page_bits - 6)) | vLineAddr & ((1UL << (_page_bits - 6)) - 1);
    // info("Page mapping: vLineAddr = %lx, vpgnum = %lx, pgnum = %lx, pLineAddr = %lx", vLineAddr, vpgnum, pgnum, pLineAddr);	
    return pLineAddr;
}

uint64_t MemoryController::access(MemReq& req) {
    // Update request state
    switch (req.type) {
        case PUTS:
        case PUTX:
            *req.state = I;
            break;
        case GETS:
            *req.state = req.is(MemReq::NOEXCL) ? S : E;
            break;
        case GETX:
            *req.state = M;
            break;
        default:
            panic("Invalid request type");
    }
    if (req.type == PUTS) return req.cycle;

    futex_lock(&_lock);

    // Handle tracing if enabled
    if (_collect_trace && _name == "mem-0") {
        handleTraceCollection(req);
    }

    _num_requests++;

    // Delegate access to CacheScheme
    Address vLineAddr = req.lineAddr;
    Address pLineAddr = mapPage(vLineAddr);
    req.lineAddr = pLineAddr;
    uint64_t result = _cache_scheme->access(req);
    req.lineAddr = vLineAddr;

    // Handle bandwidth balance if needed
    if (_bw_balance && _num_requests % _step_length == 0) {
        _cache_scheme->period(req);
    }

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

void MemoryController::printStats() {
    _ext_dram->printStats();
    for (uint32_t i = 0; i < _mcdram_per_mc; i++) _mcdram[i]->printStats();
}

DDRMemory* MemoryController::BuildDDRMemory(Config& config, uint32_t freqMHz,
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
    new (mem) DDRMemory(zinfo->lineSize, pageSize, ranksPerChannel, banksPerRank, freqMHz, tech, addrMapping, controllerLatency, queueDepth, maxRowHits, deferWrites, closedPage, domain, name, tBL, timing_scale);
    return mem;
}
