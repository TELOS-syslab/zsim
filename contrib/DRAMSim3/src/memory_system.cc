#include "memory_system.h"

namespace dramsim3 {
MemorySystem::MemorySystem(const std::string &config_file,
                           const std::string &output_dir,
                           std::function<void(uint64_t)> read_callback,
                           std::function<void(uint64_t)> write_callback, const std::string &prefix)
    : config_(new Config(config_file, output_dir, prefix)) {
    // TODO: ideal memory type?
    if (config_->IsHMC()) {
        dram_system_ = new HMCMemorySystem(*config_, output_dir, read_callback,
                                           write_callback);
    } else {
        dram_system_ = new JedecDRAMSystem(*config_, output_dir, read_callback,
                                           write_callback);
    }
}

MemorySystem::~MemorySystem() {
    delete (dram_system_);
    delete (config_);
}

void MemorySystem::ClockTick() { dram_system_->ClockTick(); }

double MemorySystem::GetTCK() const { return config_->tCK; }

int MemorySystem::GetBusBits() const { return config_->bus_width; }

int MemorySystem::GetBurstLength() const { return config_->BL; }

int MemorySystem::GetQueueSize() const { return config_->trans_queue_size; }

void MemorySystem::RegisterCallbacks(
    std::function<void(uint64_t)> read_callback,
    std::function<void(uint64_t)> write_callback) {
    dram_system_->RegisterCallbacks(read_callback, write_callback);
}

uint64_t MemorySystem::GetChannels() { return config_->channels;}
uint64_t MemorySystem::GetRanks() { return config_->ranks;}
uint64_t MemorySystem::GetBankGroups() { return config_->bankgroups;}
uint64_t MemorySystem::GetBanks() { return config_->banks;}
uint64_t MemorySystem::GetRows() { return config_->rows;}
uint64_t MemorySystem::GetColumns() { return config_->columns;}

uint64_t MemorySystem::GetChannelMask() { return config_->ch_mask;}
uint64_t MemorySystem::GetRankMask() { return config_->ra_mask;}
uint64_t MemorySystem::GetBankGroupMask() { return config_->bg_mask;}
uint64_t MemorySystem::GetBankMask() { return config_->ba_mask;}
uint64_t MemorySystem::GetRowMask() { return config_->ro_mask;}
uint64_t MemorySystem::GetColumnMask() { return config_->co_mask;}

uint64_t MemorySystem::GetChannelPosition() { return config_->ch_pos;}
uint64_t MemorySystem::GetRankPosition() { return config_->ra_pos;}
uint64_t MemorySystem::GetBankGroupPosition() { return config_->bg_pos;}
uint64_t MemorySystem::GetBankPosition() { return config_->ba_pos;}
uint64_t MemorySystem::GetRowPosition() { return config_->ro_pos;}
uint64_t MemorySystem::GetColumnPosition() { return config_->co_pos;}

uint64_t MemorySystem::Get_CL() { return config_->CL;}
uint64_t MemorySystem::Get_CWL() { return config_->CWL;}
uint64_t MemorySystem::Get_BL() { return config_->BL;}
uint64_t MemorySystem::Get_tRAS() { return config_->tRAS;}
uint64_t MemorySystem::Get_tRP() { return config_->tRP;}
uint64_t MemorySystem::Get_tRCD() { return config_->tRCD;}

bool MemorySystem::WillAcceptTransaction(uint64_t hex_addr,
                                         bool is_write) const {
    return dram_system_->WillAcceptTransaction(hex_addr, is_write);
}

bool MemorySystem::AddTransaction(uint64_t hex_addr, bool is_write) {
    return dram_system_->AddTransaction(hex_addr, is_write);
}

void MemorySystem::PrintStats() const { dram_system_->PrintStats(); }

void MemorySystem::ResetStats() { dram_system_->ResetStats(); }

MemorySystem* GetMemorySystem(const std::string &config_file, const std::string &output_dir,
                 std::function<void(uint64_t)> read_callback,
                 std::function<void(uint64_t)> write_callback, const std::string &prefix) {
    return new MemorySystem(config_file, output_dir, read_callback, write_callback, prefix);
}
}  // namespace dramsim3

// This function can be used by autoconf AC_CHECK_LIB since
// apparently it can't detect C++ functions.
// Basically just an entry in the symbol table
extern "C" {
void libdramsim3_is_present(void) { ; }
}
