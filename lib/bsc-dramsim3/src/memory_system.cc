#include "memory_system.h"

namespace dramsim3 {
MemorySystem::MemorySystem(const std::string &config_file,
                           const std::string &output_dir,
                           std::function<void(uint64_t)> read_callback,
                           std::function<void(uint64_t)> write_callback)
    : config_(new Config(config_file, output_dir)) {
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

void MemorySystem::setDelayQueue(uint32_t delayQueue){
    // cout << "debug (dramsim3): " << "delayQueue is: " << delayQueue << endl;
    DelayQueueWaitCycle = delayQueue;
}

void MemorySystem::ClockTick() { 

    vector<delayedInfo>::iterator it = queuedTransactions.begin();

    /////////////////       start of delay queue code       ///////////////////////
    // Rommel Sanchez (missing link paper. a.k.a. No Man's Land)
    // decrease delay until it is zero. then initiate the transaction. happened in Weave phase
    while(it != queuedTransactions.end()) {

        if ((*it).delayedTicks>0) {
            (*it).delayedTicks--;
            it++;
        }
        else {
	        struct delayedInfo t = *it;
            if (dram_system_->WillAcceptTransaction(t.addr, t.isWrite)) {
                if (dram_system_->AddTransaction(t.addr, t.isWrite)== false) {
                    struct delayedInfo newTransaction = {t.addr, t.isWrite, 0};
                    queuedTransactions.push_back(newTransaction);
                }
                it = queuedTransactions.erase(it);
            }
	        else {
                it++;
            }
        }
    }
    // /////////////////       end of delay queue code       ///////////////////////

    dram_system_->ClockTick(); 
}

double MemorySystem::GetTCK() const { return config_->tCK; }

int MemorySystem::GetBusBits() const { return config_->bus_width; }

int MemorySystem::GetBurstLength() const { return config_->BL; }

int MemorySystem::GetQueueSize() const { return config_->trans_queue_size; }

void MemorySystem::RegisterCallbacks(
    std::function<void(uint64_t)> read_callback,
    std::function<void(uint64_t)> write_callback) {
    dram_system_->RegisterCallbacks(read_callback, write_callback);
}

uint64_t MemorySystem::GetChannelMask() { return config_->ch_mask;}

uint64_t MemorySystem::GetRankMask() { return config_->ra_mask;}

uint64_t MemorySystem::GetBankMask() { return config_->ba_mask;}

uint64_t MemorySystem::GetRowMask() { return config_->ro_mask;}

bool MemorySystem::WillAcceptTransaction(uint64_t hex_addr,
                                         bool is_write) const {
    return dram_system_->WillAcceptTransaction(hex_addr, is_write);
}

bool MemorySystem::AddTransaction(uint64_t hex_addr, bool is_write) {
    // Delay Queue used in Missing Link paper of Rommel Sanchez.et al 
    uint64_t final_Delay_in_Q = DelayQueueWaitCycle;
    // generate delay queue. we add transactions later, after delay cycles reach zero.    
    struct delayedInfo newTransaction = {hex_addr, is_write, final_Delay_in_Q};
    queuedTransactions.push_back(newTransaction);
    return true;
   // return dram_system_->AddTransaction(hex_addr, is_write); // original code
}

void MemorySystem::PrintStats() const { dram_system_->PrintStats(); }

void MemorySystem::ResetStats() { dram_system_->ResetStats(); }

MemorySystem* GetMemorySystem(const std::string &config_file, const std::string &output_dir,
                 std::function<void(uint64_t)> read_callback,
                 std::function<void(uint64_t)> write_callback) {
    return new MemorySystem(config_file, output_dir, read_callback, write_callback);
}
}  // namespace dramsim3

// This function can be used by autoconf AC_CHECK_LIB since
// apparently it can't detect C++ functions.
// Basically just an entry in the symbol table
extern "C" {
void libdramsim3_is_present(void) { ; }
}
