#ifndef _NDC_CACHE_SCHEME_H_
#define _NDC_CACHE_SCHEME_H_

#include "cache/cache_scheme.h"
#include "mc.h"
#include "stats.h"
class NDCScheme : public CacheScheme {
   private:
    // Statistics counters
    Counter _numCleanEviction;
    Counter _numDirtyEviction;
    Counter _numLoadHit;
    Counter _numLoadMiss;
    Counter _numStoreHit;
    Counter _numStoreMiss;

    uint32_t shift_bits;
    uint32_t _ch_pos, _ra_pos, _bg_pos, _ba_pos, _ro_pos, _co_pos;
    uint32_t _ch_mask, _ra_mask, _bg_mask, _ba_mask, _ro_mask, _co_mask;

    // Mask parameter for index calculation
    uint64_t _index_mask;
    // Maximum possible address bits after removing cacheline offset bits
    static const uint32_t MAX_ADDR_BITS = 58;  // 64 - 6 bits for cache line offset

   public:
    NDCScheme(Config& config, MemoryController* mc) : CacheScheme(config, mc) {
        _scheme = NDC;  // Set scheme type (assume NDC is added to Scheme enum)
        // Configuration (e.g., _num_sets, _num_ways, _granularity, _cache) is inherited from CacheScheme
        assert(_mc->_mcdram_per_mc == 1);
        assert(_granularity == 64);
        shift_bits = 6;
        _ch_pos = config.get<uint32_t>("sys.mem.mcdram.ch_pos", 12);
        _ra_pos = config.get<uint32_t>("sys.mem.mcdram.ra_pos", 11);
        _bg_pos = config.get<uint32_t>("sys.mem.mcdram.bg_pos", 7);
        _ba_pos = config.get<uint32_t>("sys.mem.mcdram.ba_pos", 9);
        _ro_pos = config.get<uint32_t>("sys.mem.mcdram.ro_pos", 13);
        _co_pos = config.get<uint32_t>("sys.mem.mcdram.co_pos", 0);
        _ch_mask = config.get<uint32_t>("sys.mem.mcdram.ch_mask", 1);
        _ra_mask = config.get<uint32_t>("sys.mem.mcdram.ra_mask", 1);
        _bg_mask = config.get<uint32_t>("sys.mem.mcdram.bg_mask", 3);
        _ba_mask = config.get<uint32_t>("sys.mem.mcdram.ba_mask", 3);
        _ro_mask = config.get<uint32_t>("sys.mem.mcdram.ro_mask", 16383);
        _co_mask = config.get<uint32_t>("sys.mem.mcdram.co_mask", 127);

        // Get upper and lower 32-bit parts of the index mask from config
        uint32_t index_mask_upper = config.get<uint32_t>("sys.mem.mcdram.index_mask_upper", 0xFFFFFFFF);
        uint32_t index_mask_lower = config.get<uint32_t>("sys.mem.mcdram.index_mask_lower", 0xFFFFFFFF);

        // Combine into 64-bit mask
        _index_mask = (static_cast<uint64_t>(index_mask_upper) << 32) | index_mask_lower;

        // Calculate the number of bits needed for indexing sets
        uint32_t num_set_bits = 0;
        while ((1ULL << num_set_bits) < _num_sets) {
            num_set_bits++;
        }

        // Ensure the index mask has the right number of bits set
        int bits_set = 0;
        uint64_t temp_mask = _index_mask;
        while (temp_mask) {
            if (temp_mask & 1) bits_set++;
            temp_mask >>= 1;
        }

        if (bits_set != num_set_bits) {
            info("WARNING: Index mask has %d bits set, but cache needs %d bits for indexing\n",
                 bits_set, num_set_bits);

            // If we have more bits than needed, adjust the mask to use only the lowest bits
            if (bits_set > num_set_bits) {
                uint64_t new_mask = 0;
                temp_mask = _index_mask;
                int bits_used = 0;
                uint64_t bit_pos = 0;

                // Find and keep only the lowest set bits up to num_set_bits
                while (temp_mask && bits_used < num_set_bits) {
                    if (temp_mask & 1) {
                        new_mask |= (1ULL << bit_pos);
                        bits_used++;
                    }
                    temp_mask >>= 1;
                    bit_pos++;
                }

                info("Adjusting index mask to use only the lowest %d bits\n", num_set_bits);
                _index_mask = new_mask;
            }
        }
    }

    DramAddress mapAddress(Address address) const {
        uint64_t hex_addr = address >> shift_bits;
        int channel = (hex_addr >> _ch_pos) & _ch_mask;
        int rank = (hex_addr >> _ra_pos) & _ra_mask;
        int bg = (hex_addr >> _bg_pos) & _bg_mask;
        int ba = (hex_addr >> _ba_pos) & _ba_mask;
        int ro = (hex_addr >> _ro_pos) & _ro_mask;
        int co = (hex_addr >> _co_pos) & _co_mask;
        return DramAddress(channel, rank, bg, ba, ro, co);
    }

    inline uint64_t getSetNum(Address address) {
        // Using index_mask to extract index bits
        uint64_t hex_addr = address >> shift_bits;
        uint64_t index = 0;
        uint64_t mask = _index_mask;
        uint64_t bitPos = 0;
        uint64_t indexPos = 0;

        // Extract bits where mask has '1's and concatenate them
        while (mask) {
            if (mask & 1) {
                if (hex_addr & (1ULL << bitPos)) {
                    index |= (1ULL << indexPos);
                }
                indexPos++;
            }
            mask >>= 1;
            bitPos++;
        }

        return index % _num_sets;
    }

    // Get the tag from an address - tag bits are all bits that are not part of the index
    inline uint64_t getTag(Address address) {
        uint64_t hex_addr = address >> shift_bits;
        uint64_t tag = 0;
        uint64_t bitPos = 0;
        uint64_t tagPos = 0;

        // For each address bit position (excluding cache line offset)
        for (bitPos = 0; bitPos < MAX_ADDR_BITS; bitPos++) {
            // If this bit position is not part of the index mask
            if (!(_index_mask & (1ULL << bitPos))) {
                // Add it to the tag if it's set in the address
                if (hex_addr & (1ULL << bitPos)) {
                    tag |= (1ULL << tagPos);
                }
                tagPos++;
            }
        }

        return tag;
    }

    uint64_t access(MemReq& req) override;
    void period(MemReq& req) override;
    void initStats(AggregateStat* parentStat) override;
};

#endif