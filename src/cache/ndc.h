#ifndef _NDC_SCHEME_H_
#define _NDC_SCHEME_H_

#include <cstdint>

#include "cache_scheme.h"  // For CacheScheme base class
#include "g_std/g_vector.h"
#include "galloc.h"            // For GlobAlloc
#include "memory_hierarchy.h"  // For Address type
#include "stats.h"             // For AggregateStat

// Victim buffer for dirty evictions in NDC
class VictimBufferEntry {
   public:
    bool valid;
    Address address;
    uint32_t set;
    uint32_t way;
};

class VictimBuffer : public GlobAlloc {
   public:
    VictimBuffer(uint32_t size);
    ~VictimBuffer();

    bool reserveSlot();
    void releaseSlot();
    bool addEntry(Address address, uint32_t set, uint32_t way);
    bool getEntry(Address& address, uint32_t& set, uint32_t& way);
    bool isFull() { return _numEntries >= _size; }
    uint32_t getCredits() { return _size - _numEntries; }

   private:
    VictimBufferEntry* _entries;
    uint32_t _size;
    uint32_t _numEntries;
    uint32_t _head;
    uint32_t _tail;
    uint32_t _reservedSlots;
};

class NDCScheme : public CacheScheme {
   private:
    // Configuration parameters
    uint32_t _num_banks;
    double _tPCD;      // Pre-compare duration (e.g., 0.23 ns)
    double _tCOMP;     // Compare latency (e.g., 0.61 ns for 16 ways)
    double _deltaCL;   // tPCD + tCOMP
    uint32_t _tCL;     // CAS Latency
    uint32_t _tCWL;    // CAS Write Latency
    uint32_t _tRCD;    // Row Activation Delay
    uint32_t _tRP;     // Precharge Delay
    uint32_t _tCCD_L;  // Long CAS-to-CAS delay

    // Cache structure
    struct Way {
        uint64_t tag;
        bool valid;
        bool dirty;
    };
    struct Set {
        Way* ways;
    };
    Set* _cache;           // Array of sets
    uint64_t* _open_rows;  // Open row per bank (-1 if none)

    // Victim buffer
    uint32_t _victim_buffer_entries;
    VictimBuffer* _victim_buffer;

   public:
    NDCScheme(Config& config, MemoryController* mc)
        : CacheScheme(config, mc) {
        _scheme = NDC;
        _num_banks = config.get<uint32_t>("sys.mem.mcdram.num_banks", 16);
        _tPCD = config.get<double>("sys.mem.mcdram.tPCD", 0.23);
        _tCOMP = config.get<double>("sys.mem.mcdram.tCOMP", 0.61);  // For 16 ways
        _deltaCL = _tPCD + _tCOMP;
        _tCL = config.get<uint32_t>("sys.mem.mcdram.tCL", 7.5);
        _tCWL = config.get<uint32_t>("sys.mem.mcdram.tCWL", 7.5);
        _tRCD = config.get<uint32_t>("sys.mem.mcdram.tRCD", 12.5);
        _tRP = config.get<uint32_t>("sys.mem.mcdram.tRP", 12.5);
        _tCCD_L = config.get<uint32_t>("sys.mem.mcdram.tCCD_L", 2.5);

        // Initialize open rows
        _open_rows = new uint64_t[_num_banks];
        for (uint32_t b = 0; b < _num_banks; b++) {
            _open_rows[b] = static_cast<uint64_t>(-1);  // -1 indicates no open row
        }

        // Initialize victim buffer
        uint32_t vb_size = config.get<uint32_t>("sys.mem.mcdram.victim_buffer_size", 32);
        _victim_buffer = new VictimBuffer(vb_size);
        _victim_buffer_entries = 0;
    }

    ~NDCScheme() {
        for (uint32_t i = 0; i < _num_sets; i++) {
            delete[] _cache[i].ways;
        }
        delete[] _cache;
        delete[] _open_rows;
        delete _victim_buffer;
    }

    uint32_t selectVictim(uint64_t index) {
        // Random replacement policy (Section IV-C)
        return rand() % _num_ways;
    }

    uint64_t access(MemReq& req) override;
    void period(MemReq& req) override;
    void initStats(AggregateStat* parentStat) override;
};

#endif  // _NDC_SCHEME_H_