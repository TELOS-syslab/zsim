#ifndef __MEMORY_SYSTEM__H
#define __MEMORY_SYSTEM__H

#include <functional>
#include <string>

#include "configuration.h"
#include "dram_system.h"
#include "hmc.h"

namespace dramsim3 {

// This should be the interface class that deals with CPU
class MemorySystem {
   public:
    MemorySystem(const std::string &config_file, const std::string &output_dir,
                 std::function<void(uint64_t)> read_callback,
                 std::function<void(uint64_t)> write_callback, const std::string &prefix="");
    ~MemorySystem();
    void ClockTick();
    void RegisterCallbacks(std::function<void(uint64_t)> read_callback,
                           std::function<void(uint64_t)> write_callback);
    double GetTCK() const;
    int GetBusBits() const;
    int GetBurstLength() const;
    int GetQueueSize() const;
    void PrintStats() const;
    void ResetStats();

    uint64_t GetChannels();
	uint64_t GetRanks();
    uint64_t GetBankGroups();
	uint64_t GetBanks();
	uint64_t GetRows();
	uint64_t GetColumns();

    uint64_t GetChannelMask();
	uint64_t GetRankMask();
    uint64_t GetBankGroupMask();
	uint64_t GetBankMask();
	uint64_t GetRowMask();
	uint64_t GetColumnMask();

	uint64_t GetChannelPosition();
	uint64_t GetRankPosition();
	uint64_t GetBankGroupPosition();
	uint64_t GetBankPosition();
	uint64_t GetRowPosition();
    uint64_t GetColumnPosition();

    uint64_t Get_CL();
    uint64_t Get_CWL();
    uint64_t Get_BL();
    uint64_t Get_tRAS();
    uint64_t Get_tRP();
    uint64_t Get_tRCD();

    bool WillAcceptTransaction(uint64_t hex_addr, bool is_write) const;
    bool AddTransaction(uint64_t hex_addr, bool is_write);

   private:
    // These have to be pointers because Gem5 will try to push this object
    // into container which will invoke a copy constructor, using pointers
    // here is safe
    Config *config_;
    BaseDRAMSystem *dram_system_;
};

MemorySystem* GetMemorySystem(const std::string &config_file, const std::string &output_dir,
                 std::function<void(uint64_t)> read_callback,
                 std::function<void(uint64_t)> write_callback, const std::string &prefix="");

}  // namespace dramsim3

#endif
