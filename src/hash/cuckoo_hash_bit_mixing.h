#ifndef ZSIM_HASH_CUCKOO_HASH_BIT_MIXING_H_
#define ZSIM_HASH_CUCKOO_HASH_BIT_MIXING_H_

#include <algorithm> // For std::swa
#include <queue>
#include <random>
#include <vector>
#include <unordered_map>

#include <cassert>
#include <cstdint>

#include "hash.h"
#include "cuckoo_hash.h"

class CuckooHashBitMixing: public CuckooHash {
    public:
        CuckooHashBitMixing(uint64_t dram_set_idx, uint64_t page_size, uint64_t cache_size, 
            uint64_t nr_bucket, uint64_t hash_assoc, uint64_t bucket_assoc, uint64_t target_load_ratio):
            CuckooHash(dram_set_idx, page_size, cache_size, nr_bucket, hash_assoc, bucket_assoc, target_load_ratio)
            {
                if (nr_bucket != (uint64_t)1 << nr_mixing_bit_) {
                    printf("Bix Mixing only support 16MB map uint\n");
                    assert(0);
                }
                InitBitMixConfig();
            }

    protected:
        uint64_t Hash(uint64_t key, uint64_t map_hash_idx, uint64_t map_unit_idx_in_group) override;
        uint64_t _GetTargetSetIdxDefault(uint64_t phy_cache_addr, uint64_t map_unit_idx_in_group) override;

    private:
        uint64_t nr_mixing_bit_ = 18;   // FIMXE 硬编码
        uint64_t max_map_ratio_ = 16;    // FIXME 将硬编码改为可以调整的编码
        std::vector<uint64_t> rotl_list_;
        std::vector<uint64_t> xor_mask_list_;

        void InitBitMixConfig(void);

        // 用于进行bit处理的函数
        // 位反转函数（18位）
        uint64_t ReverseBits(uint64_t key, int n = 18);
        // 循环左移函数（18位）
        uint64_t Rotl(uint64_t key, int offset, int n = 18);
        // 反转 -> 移位 -> 异或掩码
        uint64_t BixMixing(uint64_t key, uint64_t hash_idx);

};

#endif