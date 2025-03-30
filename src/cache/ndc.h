#ifndef _NDC_CACHE_SCHEME_H_
#define _NDC_CACHE_SCHEME_H_

#include <cmath>  // For log2
#include <string>

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

    uint32_t _ch_pos, _ra_pos, _bg_pos, _ba_pos, _ro_pos, _co_pos;
    uint32_t _ch_mask, _ra_mask, _bg_mask, _ba_mask, _ro_mask, _co_mask;

    uint64_t _ext_size;
    // Mask parameters for address translation
    uint64_t _index_mask, _cache_tag_mask, _pred_tag_mask;
    uint32_t _num_index_bits, _num_cache_tag_bits, _num_pred_tag_bits, _num_cache_bits, _num_ext_bits, _num_shift_bits;

    // Maximum possible address bits after removing cacheline offset bits
    static const uint32_t MAX_ADDR_BITS = 58;  // 64 - 6 bits for cache line offset

   public:
    NDCScheme(Config& config, MemoryController* mc) : CacheScheme(config, mc) {
        _scheme = NDC;
        assert(_mc->_mcdram_per_mc == 1);
        assert(_granularity == 64);
        _num_shift_bits = 6;

        // Get DRAM address mapping parameters
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

        _ext_size = (uint64_t)config.get<uint32_t>("sys.mem.ext_dram.size", 128) * 1024 * 1024;
        // Calculate number of tag bits based on number of ways
        _num_cache_tag_bits = ceil(log2(_num_ways));
        _num_index_bits = ceil(log2(_num_sets));
        _num_cache_bits = _num_cache_tag_bits + _num_index_bits;
        _num_ext_bits = ceil(log2(_ext_size / _granularity));
        _num_pred_tag_bits = _num_ext_bits - _num_cache_bits;
        _pred_tag_mask = ((1ULL << _num_pred_tag_bits) - 1) << _num_cache_bits;
        assert(_num_cache_bits <= _num_ext_bits);
        assert(_num_cache_tag_bits <= _co_mask + 1);

        // Get user-specified index mask if provided, otherwise use default
        uint32_t index_mask_upper = config.get<uint32_t>("sys.mem.mcdram.index_mask_upper", 0x0);
        uint32_t index_mask_lower = config.get<uint32_t>("sys.mem.mcdram.index_mask_lower", 0x0);

        if (index_mask_upper == 0 && index_mask_lower == 0) {
            // Create default index mask with bits set from bit position _num_cache_tag_bits upward
            _index_mask = ((1ULL << _num_index_bits) - 1) << _num_cache_tag_bits;
        } else {
            // Use provided index mask
            _index_mask = (static_cast<uint64_t>(index_mask_upper) << 32) | index_mask_lower;

            // Count bits in the provided mask
            int bits_set = 0;
            uint64_t temp_mask = _index_mask;
            while (temp_mask) {
                if (temp_mask & 1) bits_set++;
                temp_mask >>= 1;
            }
            assert(bits_set == _num_index_bits);
        }

        // Calculate tag mask as the inverse of index mask (within valid address bits)
        _cache_tag_mask = ~_index_mask & ((1ULL << _num_cache_bits) - 1);

        // Create string representations of the masks
        std::string index_mask_str = "";
        std::string tag_mask_str = "";

        // Build binary strings from MSB to LSB
        for (int i = _num_cache_bits - 1; i >= 0; i--) {
            index_mask_str += ((_index_mask >> i) & 1) ? '1' : '0';
            tag_mask_str += ((_cache_tag_mask >> i) & 1) ? '1' : '0';

            // Add space every 8 bits for readability
            if (i % 8 == 0 && i > 0) {
                index_mask_str += ' ';
                tag_mask_str += ' ';
            }
        }

        // Output both hexadecimal values and binary strings
        info("index_mask = 0x%lx (%s); tag_mask = 0x%lx (%s)\n", _index_mask, index_mask_str.c_str(), _cache_tag_mask, tag_mask_str.c_str());
    }

    DramAddress mapAddress(Address address) const {
        uint64_t hex_addr = address >> _num_shift_bits;
        int channel = (hex_addr >> _ch_pos) & _ch_mask;
        int rank = (hex_addr >> _ra_pos) & _ra_mask;
        int bg = (hex_addr >> _bg_pos) & _bg_mask;
        int ba = (hex_addr >> _ba_pos) & _ba_mask;
        int ro = (hex_addr >> _ro_pos) & _ro_mask;
        int co = (hex_addr >> _co_pos) & _co_mask;
        return DramAddress(channel, rank, bg, ba, ro, co);
    }

    // Ultra-optimized physical address to cache address conversion
    inline Address phyAddr2cacheAddr(Address phy_addr) {
        // Shift out cache line offset bits
        uint64_t hex_addr = phy_addr >> _num_shift_bits;

        // Initialize the cache address
        uint64_t cache_addr = 0;

        // 1. EXTRACT TAG BITS
        // Rather than checking each bit individually, extract and compress tag bits in one go
        uint64_t tag_value = 0;
        uint64_t bit_pos = 0;
        uint64_t mask = 1;

        // Find first tag bit position
        int first_tag_bit = 0;
        while (first_tag_bit < 64 && !(_cache_tag_mask & (1ULL << first_tag_bit))) {
            first_tag_bit++;
        }

        // Extract tag bits using direct bit manipulation
        for (int i = first_tag_bit; i < _num_cache_bits && bit_pos < _num_cache_tag_bits; i++) {
            if (_cache_tag_mask & (1ULL << i)) {
                // Extract this bit directly - no conditional
                tag_value |= ((hex_addr >> i) & 1) << bit_pos;
                bit_pos++;
            }
        }

        // Place tag bits directly at column position
        cache_addr |= (tag_value << _co_pos);

        // 2. EXTRACT AND PLACE INDEX BITS
        // Extract index bits directly
        uint64_t index_value = 0;
        bit_pos = 0;

        // Find first index bit position
        int first_index_bit = 0;
        while (first_index_bit < 64 && !(_index_mask & (1ULL << first_index_bit))) {
            first_index_bit++;
        }

        // Extract index bits using direct bit manipulation
        for (int i = first_index_bit; i < _num_cache_bits && bit_pos < _num_index_bits; i++) {
            if (_index_mask & (1ULL << i)) {
                // Extract this bit directly
                index_value |= ((hex_addr >> i) & 1) << bit_pos;
                bit_pos++;
            }
        }

        // Place index bits in non-tag positions
        bit_pos = 0;
        for (int i = 0; i < _num_cache_bits && bit_pos < _num_index_bits; i++) {
            // Skip the tag positions
            if (i >= _co_pos && i < (_co_pos + _num_cache_tag_bits)) {
                continue;
            }

            // Place next index bit directly - no conditional
            cache_addr |= ((index_value >> bit_pos) & 1) << i;
            bit_pos++;
        }

        cache_addr = (cache_addr << _num_shift_bits) | (phy_addr & ((1ULL << _num_shift_bits) - 1)) | (phy_addr & (_pred_tag_mask << _num_shift_bits));
        return cache_addr;
    }

    inline uint64_t getSetNum(Address cache_addr) {
        uint64_t hex_addr = cache_addr >> _num_shift_bits;
        uint64_t index = 0;
        uint64_t bit_pos = 0;

        // Place bits directly using shifts and masks
        for (int i = 0; i < _num_cache_bits && bit_pos < _num_index_bits; i++) {
            // Skip tag positions
            if (i >= _co_pos && i < (_co_pos + _num_cache_tag_bits)) {
                continue;
            }

            // Extract and place bit directly - no conditional
            index |= ((hex_addr >> i) & 1) << bit_pos;
            bit_pos++;
        }

        return index;
    }

    // Get tag from cache address - optimized version
    inline uint64_t getTag(Address cache_addr) {
        uint64_t hex_addr = cache_addr >> _num_shift_bits;
        return (hex_addr >> _co_pos) & ((1ULL << _num_cache_tag_bits) - 1) | (cache_addr & (_pred_tag_mask << _num_shift_bits));
    }

    uint64_t access(MemReq& req) override;
    void period(MemReq& req) override;
    void initStats(AggregateStat* parentStat) override;
};

#endif