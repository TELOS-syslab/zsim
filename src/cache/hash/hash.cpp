#include "hash.h"

#include <queue>

#include <cassert>
#include <cmath>

uint64_t XXHash(uint64_t key)
{
    uint64_t prime1 = 0x9E3779B185EBCA87ULL;
    uint64_t prime2 = 0xC2B2AE3D27D4EB4FULL;
    uint64_t prime3 = 0x165667B19E3779F9ULL;

    key += prime1;
    key ^= key >> 33;
    key *= prime2;
    key ^= key >> 29;
    key *= prime3;
    key ^= key >> 32;

    return key;
}

uint64_t BobHash(uint64_t key)
{
    key = (key + 0x7ed55d166bef7a3dULL) + (key << 12);
    key = (key ^ 0xc761c23c510fa2ddULL) ^ (key >> 19);
    key = (key + 0x165667b19e3779f9ULL) + (key << 5);
    key = (key + 0xd3a2646cabf5d9e4ULL) ^ (key << 9);
    key = (key + 0xfd7046c5ef7d0c23ULL) + (key << 3);
    key = (key ^ 0xb55a4f09a1cba50cULL) ^ (key >> 16);
    return key;
}

uint64_t CityHash(uint64_t key)
{
    uint64_t k1 = 0xc3a5c85c97cb3127ULL;
    uint64_t k2 = 0xb492b66fbe98f273ULL;
    //uint64_t k3 = 0x9ae16a3b2f90404fULL;

    key ^= key >> 33;
    key *= k1;
    key ^= key >> 29;
    key *= k2;
    key ^= key >> 32;

    return key;
}

// 构造函数：保存桶数并初始化 LCG 参数
LCGHash::LCGHash(uint64_t nr_bucket)
    : nr_bucket_(nr_bucket) {
    InitLCGEntry();
}

// 快速计算逆元：牛顿迭代法（适用于模数为2^k，且 a 为奇数）
uint64_t LCGHash::ModularInverse(uint64_t a) const {
    assert(a % 2 == 1);
    uint64_t inv = a;
    for (uint64_t i = 0; i < 5; ++i) { // 5 次迭代精度足够
        inv = inv * (2 - a * inv);
    }
    return inv;
}

// 初始化 LCG 参数（这里硬编码两个哈希函数的参数）
void LCGHash::InitLCGEntry() {
    lcg_list_.resize(2);

    lcg_list_[0] = LCG_entry{
        0x9E3779B9UL, // a
        0,            // a_rev 待计算
        0xB7E15162UL  // b
    };

    lcg_list_[1] = LCG_entry{
        0x6C078965UL, // a
        0,            // a_rev 待计算
        0xCAFEBABEUL  // b
    };

    // 计算每个参数 a 的逆元
    for (auto& lcg : lcg_list_) {
        lcg.a_rev = ModularInverse(lcg.a);
    }

    // 可选：确保 lcg_list_ 的大小与预期一致
    assert(lcg_list_.size() == 2);
}

// 对外接口：根据 key 和指定的哈希函数下标计算哈希值
uint64_t LCGHash::LCG_hash(uint64_t key, uint64_t hash_index) const {
    assert(hash_index < lcg_list_.size());
    const LCG_entry& lcg = lcg_list_[hash_index];
    return (lcg.a * key + lcg.b) % nr_bucket_;
}

// 对外接口：根据桶索引和指定的哈希函数下标计算逆哈希值
uint64_t LCGHash::LCG_rev_hash(uint64_t bucket_idx, uint64_t hash_index) const {
    assert(hash_index < lcg_list_.size());
    const LCG_entry& lcg = lcg_list_[hash_index];
    return (lcg.a_rev * (bucket_idx + nr_bucket_ - lcg.b)) % nr_bucket_;
}

//------------------------------------MagicOffsetHash-----------------------------------------//
// 
MagicOffsetHash::MagicOffsetHash(uint64_t nr_bucket)
{
    nr_bucket_ = nr_bucket;
    magic_number_ = 0x9E3779B9UL;
}

uint64_t MagicOffsetHash::Hash(uint64_t key)
{
    return (key + magic_number_) % nr_bucket_;
}
    
//------------------------------------RowBufferHitAddrHash-----------------------------------------//
RowBufferHitAddrHash::RowBufferHitAddrHash(uint64_t nr_bucket)
    : nr_bucket_(nr_bucket), nr_line_in_row_(2),
    lcg_row_(nr_bucket_ / nr_line_in_row_),
    lcg_line_(nr_line_in_row_)
{}

uint64_t RowBufferHitAddrHash::Hash(uint64_t phy_line_addr)
{
    // 先转换为保证DRAM Row Buffer Hit的形态
    // 对于phy_cache_addr, 利用[0:1]和[6:63]进行索引
    uint64_t low2 = phy_line_addr & 0x3;       // 低2位不变
    uint64_t high_part = phy_line_addr >> (2 + (uint64_t)log2(nr_line_in_row_));     // 现在提取第6～63位
    uint64_t origin_row_index = (high_part << 2) | low2;   // 拼接新数据

    uint64_t seq_line_addr = origin_row_index * nr_line_in_row_
        + ((phy_line_addr >> 2) & (nr_line_in_row_ - 1));

    // 再进行shuffle
    uint64_t line_addr_in_row = seq_line_addr % nr_line_in_row_;
    uint64_t shuffle_addr_in_row = lcg_line_.LCG_hash(line_addr_in_row, 0);

    uint64_t row_idx = seq_line_addr / nr_line_in_row_;
    uint64_t shuffled_row_idx = lcg_row_.LCG_hash(row_idx, 0);
    assert(shuffled_row_idx < nr_bucket_ / nr_line_in_row_);

    uint64_t shuffled_line_addr = shuffled_row_idx * nr_line_in_row_ + shuffle_addr_in_row;
    assert(shuffled_line_addr < nr_bucket_);

    return shuffled_line_addr;
}

//------------------------------------NextLine-----------------------------------------//
NextLineHash::NextLineHash(uint64_t nr_bucket)
{
    nr_bucket_ = nr_bucket;
}

uint64_t NextLineHash::Hash(uint64_t key, uint64_t hash_idx)
{
    uint64_t pos = 0;
    if (hash_idx == 0) {
        pos = key;
    }else if (hash_idx == 1) {
        pos = (key + 1) % nr_bucket_;
    } else {
        assert(0);
    }

    return pos;
}
