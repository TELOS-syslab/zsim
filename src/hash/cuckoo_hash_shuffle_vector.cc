#include "cuckoo_hash_shuffle_vector.h"

void CuckooHashShuffleVector::knuthFisherYatesShuffle(std::vector<uint64_t>& arr, std::mt19937& rng)
{
    int n = arr.size();
    std::uniform_int_distribution<> dist(0, n - 1); // Uniform distribution for random indices

    // 从最后一个元素开始，逐步向前
    for (int i = n - 1; i > 0; --i) {
        // 生成一个随机索引 j，0 <= j <= i
        int j = dist(rng);
        // 交换 arr[i] 和 arr[j]
        std::swap(arr[i], arr[j]);
    }
}

void CuckooHashShuffleVector::InitShuffleVec(void)
{
    std::random_device rd;  // 获取硬件随机种子
    std::vector<std::mt19937> rng_list;
    for (uint64_t idx = 0; idx < nr_shuffle_vec_; idx += 1) {
        rng_list.push_back(std::mt19937(rd()));
    }

    for (uint64_t vec_idx = 0; vec_idx < nr_shuffle_vec_; vec_idx += 1) {
        for (uint64_t entry_idx = 0; entry_idx < nr_shuffle_entry_; entry_idx += 1) {
            assert(vec_idx < shuffle_vec_list_.size());
            assert(entry_idx < shuffle_vec_list_[vec_idx].size());

            shuffle_vec_list_[vec_idx][entry_idx] = entry_idx;
        }
    }

    for (uint64_t idx = 0; idx < nr_shuffle_vec_; idx += 1) {
        knuthFisherYatesShuffle(shuffle_vec_list_[idx], rng_list[idx]);
    }    
}

uint64_t CuckooHashShuffleVector::Hash(uint64_t key, uint64_t map_hash_idx, uint64_t map_unit_idx_in_group)
{
    uint64_t cache_idx_in_map_unit = key % nr_set_per_page_;
    
    uint64_t shuffle_idx = cache_idx_in_map_unit / nr_cache_per_shuffle_entry_;
    uint64_t cache_idx_in_shuffle_entry = cache_idx_in_map_unit % nr_cache_per_shuffle_entry_;
    assert(shuffle_idx < nr_shuffle_entry_);

    uint64_t hash_idx = map_unit_idx_in_group * 2 + map_hash_idx;
    assert(map_hash_idx < 2);
    assert(hash_idx < shuffle_vec_list_.size());
    assert(shuffle_idx < shuffle_vec_list_[hash_idx].size());

    uint64_t target_shuffle_entry_idx = shuffle_vec_list_[hash_idx][shuffle_idx];
    assert(target_shuffle_entry_idx < nr_shuffle_entry_);

    uint64_t target_cache_in_map_unit = target_shuffle_entry_idx * nr_cache_per_shuffle_entry_
        + cache_idx_in_shuffle_entry;
    assert(target_cache_in_map_unit < nr_set_per_page_);
    return target_cache_in_map_unit;
}

uint64_t CuckooHashShuffleVector::_GetTargetSetIdxDefault(uint64_t phy_cache_addr, uint64_t map_unit_idx_in_group)
{
    return XXHash(phy_cache_addr) % nr_set_per_page_;
}