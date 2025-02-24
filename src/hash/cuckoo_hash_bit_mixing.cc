#include "cuckoo_hash_bit_mixing.h"

void CuckooHashBitMixing::InitBitMixConfig(void) 
{
    rotl_list_.resize(max_map_ratio_ * 2);
    xor_mask_list_.resize(max_map_ratio_ * 2);

    std::vector<uint64_t> rotl_candidate_list = {5, 7, 11, 13};
    std::vector<uint64_t> xor_candidate_mark_list = {
        0x2AAAA, 0x15555, 0x1C71C, 0x0E38E
    };

    for (uint64_t idx = 0; idx < rotl_list_.size(); idx += 1) {
        uint64_t offset_idx = idx % rotl_candidate_list.size();
        assert(offset_idx < rotl_candidate_list.size());
        assert(idx < rotl_list_.size());
        rotl_list_[idx] = rotl_candidate_list[offset_idx];

        uint64_t mask_idx = idx % xor_candidate_mark_list.size();
        assert(mask_idx < xor_candidate_mark_list.size());
        assert(idx < xor_mask_list_.size());
        xor_mask_list_[idx] = xor_candidate_mark_list[mask_idx];
    }
}

// 位反转函数（18位）
uint64_t CuckooHashBitMixing::ReverseBits(uint64_t key, int n)
{
    uint64_t result = 0;
    for (int i = 0; i < n; ++i) {
        result <<= 1;
        result |= (key & 1);
        key >>= 1;
    }
    return result;
}
        
// 循环左移函数（18位）
uint64_t CuckooHashBitMixing::Rotl(uint64_t key, int offset, int n)
{
    offset %= n;
    return ((key << offset) | (key >> (n - offset))) & ((1 << n) - 1);
}

uint64_t CuckooHashBitMixing::BixMixing(uint64_t key, uint64_t hash_idx)
{
    assert(hash_idx < rotl_list_.size());
    assert(hash_idx < xor_mask_list_.size());
    uint64_t reversed = ReverseBits(key);
    uint64_t rotated = Rotl(reversed, rotl_list_[hash_idx]);
    return rotated ^ xor_mask_list_[hash_idx];
}

uint64_t CuckooHashBitMixing::Hash(uint64_t key, uint64_t map_hash_idx, uint64_t map_unit_idx_in_group)
{
    uint64_t cache_idx_in_map_unit = key % nr_set_per_page_;
    assert(cache_idx_in_map_unit < ((uint64_t)1 << nr_mixing_bit_));

    uint64_t hash_idx = map_unit_idx_in_group * 2 + map_hash_idx;
    assert(hash_idx < rotl_list_.size());
    assert(hash_idx < xor_mask_list_.size());

    uint64_t target_cache_in_map_unit = BixMixing(key, hash_idx);
    assert(target_cache_in_map_unit < ((uint64_t)1 << nr_mixing_bit_));

    return target_cache_in_map_unit;
}

uint64_t CuckooHashBitMixing::_GetTargetSetIdxDefault(uint64_t phy_cache_addr, uint64_t map_unit_idx_in_group)
{
    return XXHash(phy_cache_addr) % nr_set_per_page_;
}
