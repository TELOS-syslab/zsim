#include "hash.h"

#include <queue>

#include <cassert>

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