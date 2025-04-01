#ifndef CACHE_UTILS_H
#define CACHE_UTILS_H

#include "config.h"
#include "g_std/g_string.h"
#include "memory_hierarchy.h"

enum Scheme {
    AlloyCache,
    UnisonCache,
    BansheeCache,
    NoCache,
    CacheOnly,
    CopyCache,
    NDC,
    IdealBalanced,
    IdealAssociative,
    IdealFully,
    IdealHotness,
    CHAMO,
    UNKNOWN
};

enum ReqType {
    LOAD = 0,
    STORE
};

class Way {
   public:
    Address tag;
    bool valid;
    bool dirty;
};

class Set {
   public:
    Way* ways;
    uint32_t num_ways;

    uint32_t getEmptyWay() {
        for (uint32_t i = 0; i < num_ways; i++)
            if (!ways[i].valid)
                return i;
        return num_ways;
    };
    bool hasEmptyWay() { return getEmptyWay() < num_ways; };
};

class TLBEntry {
   public:
    uint64_t tag;
    uint64_t way;
    uint64_t count;  // for OS based placement policy

    // the following two are only for UnisonCache
    // due to space cosntraint, it is not feasible to keep one bit for each line,
    // so we use 1 bit for 4 lines.
    uint64_t touch_bitvec;  // whether a line is touched in a page
    uint64_t dirty_bitvec;  // whether a line is dirty in page
};

class LineEntry {
   public:
    uint64_t way;
};

class TagBufferEntry {
   public:
    Address tag;
    bool remap;
    uint32_t lru;
};

class TagBuffer : public GlobAlloc {
   public:
    TagBuffer(Config& config);
    // return: exists in tag buffer or not.
    uint32_t existInTB(Address tag);
    uint32_t getNumWays() { return _num_ways; };

    // return: if the address can be inserted to tag buffer or not.
    bool canInsert(Address tag);
    bool canInsert(Address tag1, Address tag2);
    void insert(Address tag, bool remap);
    double getOccupancy() { return 1.0 * _entry_occupied / _num_ways / _num_sets; };
    void clearTagBuffer();
    void setClearTime(uint64_t time) { _last_clear_time = time; };
    uint64_t getClearTime() { return _last_clear_time; };

   private:
    void updateLRU(uint32_t set_num, uint32_t way);
    TagBufferEntry** _tag_buffer;
    uint32_t _num_ways;
    uint32_t _num_sets;
    uint32_t _entry_occupied;
    uint64_t _last_clear_time;
};

struct DramAddress {
    DramAddress()
        : channel(-1), rank(-1), bankgroup(-1), bank(-1), row(-1), column(-1) {}
    DramAddress(int channel, int rank, int bankgroup, int bank, int row, int column)
        : channel(channel),
          rank(rank),
          bankgroup(bankgroup),
          bank(bank),
          row(row),
          column(column) {}
    DramAddress(const DramAddress& addr)
        : channel(addr.channel),
          rank(addr.rank),
          bankgroup(addr.bankgroup),
          bank(addr.bank),
          row(addr.row),
          column(addr.column) {}
    int channel;
    int rank;
    int bankgroup;
    int bank;
    int row;
    int column;
};
#endif