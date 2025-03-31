#ifndef ZSIM_HASH_H_
#define ZSIM_HASH_H_

#include <vector>
#include <unordered_map>

#include <cassert>
#include <cstdint>

uint64_t XXHash(uint64_t key);
uint64_t BobHash(uint64_t key);
uint64_t CityHash(uint64_t key);

#endif