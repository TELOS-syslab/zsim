#ifndef _HASH_H_
#define _HASH_H_

#include <string>
#include <vector>
#include <unordered_map>

#include <cassert>
#include <cstdint>

enum HashType {
    kXXHashFunc = 0,
    kBobHashFunc,
    kCityHashFunc,
    kLCGHashFunc,
    kMagicOffsetHashFunc,
    kRowBufferHitAddrHashFunc,
    kNextLineHashFunc,
    kHashTypeEnd
};

const std::string kHashStr[] = {"XXHash", "BobHash", "CityHash", "LCG", "MagicOffset", 
    "RowBufferHitAddrHash", "NextLine"};

uint64_t XXHash(uint64_t key);
uint64_t BobHash(uint64_t key);
uint64_t CityHash(uint64_t key);

class LCGHash {
public:
    // 构造函数，传入桶的总数（模数）
    explicit LCGHash(uint64_t nr_bucket);

    // 根据 key 和哈希函数下标计算哈希值
    uint64_t LCG_hash(uint64_t key, uint64_t hash_index) const;
    // 根据桶索引和哈希函数下标计算逆哈希值
    uint64_t LCG_rev_hash(uint64_t bucket_idx, uint64_t hash_index) const;

private:
    // 内部结构：存储 LCG 的参数
    struct LCG_entry {
        uint64_t a;      // 用于计算 hash: (a * key + b) % nr_bucket
        uint64_t a_rev;  // 参数 a 的逆元，用于逆向计算
        uint64_t b;
    };

    uint64_t nr_bucket_;            // 桶的总数，即模数
    std::vector<LCG_entry> lcg_list_; // 存储多个 LCG 参数

    // 快速计算参数 a 的逆元（要求 a 为奇数）
    uint64_t ModularInverse(uint64_t a) const;
    // 初始化 LCG 参数，本例中硬编码两个参数
    void InitLCGEntry();
};

class MagicOffsetHash {
public:
    // 构造函数，传入桶的总数（模数）
    explicit MagicOffsetHash(uint64_t nr_bucket);
    uint64_t Hash(uint64_t key);

private:
    uint64_t nr_bucket_;            // 桶的总数，即模数
    uint64_t magic_number_;

};

class RowBufferHitAddrHash {
public:
    // 构造函数，传入桶的总数（模数）
    explicit RowBufferHitAddrHash(uint64_t nr_bucket);  // nr_cxl_line
    uint64_t Hash(uint64_t phy_line_addr);

private:
    uint64_t nr_bucket_;            // 桶的总数，即模数
    uint64_t nr_line_in_row_;  // 最小的连续的cache block个数
    LCGHash lcg_row_;   // 用于shuffle不同row
    LCGHash lcg_line_;  // 用于shuffle同一个row的不同line
};

class NextLineHash {
public:
    explicit NextLineHash(uint64_t nr_bucket);
    uint64_t Hash(uint64_t key, uint64_t hash_idx);

private:
    uint64_t nr_bucket_;
};

#endif