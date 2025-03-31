#ifndef ZSIM_HASH_CUCKOO_HASH_SHUFFLE_VECTOR_H_
#define ZSIM_HASH_CUCKOO_HASH_SHUFFLE_VECTOR_H_

#include <algorithm> // For std::swa
#include <queue>
#include <random>
#include <vector>
#include <unordered_map>

#include <cassert>
#include <cstdint>

#include "hash.h"
#include "cuckoo_hash.h"

class CuckooHashShuffleVector: public CuckooHash {
    public:
        CuckooHashShuffleVector(uint64_t dram_set_idx, uint64_t page_size, uint64_t cache_size, 
            uint64_t nr_bucket, uint64_t hash_assoc, uint64_t bucket_assoc, uint64_t target_load_ratio,
            uint64_t nr_shuffle_entry):
            CuckooHash(dram_set_idx, page_size, cache_size, nr_bucket, hash_assoc, bucket_assoc, target_load_ratio),
            nr_shuffle_entry_(nr_shuffle_entry), nr_cache_per_shuffle_entry_(nr_set_per_page_ / nr_shuffle_entry),
            nr_shuffle_vec_(32), shuffle_vec_list_(nr_shuffle_vec_, std::vector<uint64_t>(nr_shuffle_entry, 0))
            {
                assert(bucket_assoc_ == 1);
                InitShuffleVec();
            }

    protected:
        uint64_t Hash(uint64_t key, uint64_t map_hash_idx, uint64_t map_unit_idx_in_group) override;
        uint64_t _GetTargetSetIdxDefault(uint64_t phy_cache_addr, uint64_t map_unit_idx_in_group) override;

    private:
        uint64_t nr_shuffle_entry_;
        uint64_t nr_cache_per_shuffle_entry_;
        uint64_t nr_shuffle_vec_;
        std::vector<std::vector<uint64_t>> shuffle_vec_list_;

        void knuthFisherYatesShuffle(std::vector<uint64_t>& arr, std::mt19937& rng); // 构建shuffle vector的算法
        void InitShuffleVec(void);
};

#endif