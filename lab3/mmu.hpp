#pragma once
#ifndef mmu_hpp
#define mmu_hpp
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <queue>

using namespace std;

namespace mmu {

unsigned int        MAX_VPAGE       = 64;
unsigned int        NUM_FRAMES      = 4;
unsigned int        CURRENT_PID     = 0;
unsigned long       INSTR_COUNT     = 0;
unsigned long       CTX_SWITCHES    = 0;
unsigned long       PROCESS_EXITS   = 0;
unsigned long long  COST            = 0;

struct Frame;
struct Process;

vector<Frame>       frame_table;
deque<unsigned int> free_frames;
vector<Process>     process_pool;

enum COST_TABLE {
    READ_WRITE      = 1,
    SWITCHES        = 130,
    EXITS           = 1250,
    MAPS            = 300,
    UNMAPS          = 400,
    INS             = 3100,
    OUTS            = 2700,
    FINS            = 2800,
    FOUTS           = 2400,
    ZEROS           = 140,
    SEGV            = 340,
    SEGPROT         = 420
};

struct PTE {
    unsigned int present:1;
    unsigned int referenced:1;
    unsigned int modified:1;
    unsigned int paged_out:1;
    unsigned int frame:7;
    unsigned int vma_checked:1;
    unsigned int write_protect:1;
    unsigned int file_mapped:1;
    unsigned int vma_valid:1;
    unsigned int zeros:17;
};

struct VMA {
    unsigned int start_vpage:6;
    unsigned int end_vpage:6;
    unsigned int write_protect:1;
    unsigned int file_mapped:1;
};

struct Frame {
    unsigned int pid_rv:4;
    unsigned int vpage_rv:6;
    unsigned int mapped:1;
    unsigned int age;
};

struct Process {
    vector<VMA> vmas;
    vector<PTE> page_table;
    unsigned long unmaps    = 0;
    unsigned long maps      = 0;
    unsigned long ins       = 0;
    unsigned long fins      = 0;
    unsigned long outs      = 0;
    unsigned long fouts     = 0;
    unsigned long zeros     = 0;
    unsigned long segv      = 0;
    unsigned long segprot   = 0;
    
    Process() : page_table(MAX_VPAGE) {}
};

inline PTE& reversed_map(int i) {
    unsigned int pid_rv = frame_table[i].pid_rv;
    unsigned int vpage_rv = frame_table[i].vpage_rv;
    return process_pool[pid_rv].page_table[vpage_rv];
}

inline Process& current_process() {
    return process_pool[CURRENT_PID];
}

class Pager {
protected:
    const bool OPTION_a;
public:
    Pager(bool OPTION_a) : OPTION_a(OPTION_a) {}
    virtual ~Pager() {}
    virtual void age_operation(unsigned int frame) {}
    virtual unsigned int select_victim_frame() = 0;
};

class FifoPager : public Pager {
protected:
    unsigned int hand;
public:
    FifoPager(bool OPTION_a) : Pager(OPTION_a), hand(0) {}
    
    virtual unsigned int select_victim_frame() {
        hand = hand % NUM_FRAMES;
        if (OPTION_a) {
            cout << "ASELECT " << hand << endl;
        }
        return hand++;
    }
};

class ClockPager : public FifoPager {
public:
    ClockPager(bool OPTION_a) : FifoPager(OPTION_a) {}
    
    unsigned int select_victim_frame() {
        unsigned int start = hand;
        unsigned int counter = 0;
        while (true) {
            counter++;
            PTE& pte_rv = reversed_map(hand);
            if (pte_rv.referenced) {
                pte_rv.referenced = false;
                hand = (hand+1) % NUM_FRAMES;
            }
            else break;
        }
        
        if (OPTION_a) {
            cout << "ASELECT " << start
                 << " " << counter << endl;
        }
        
        unsigned int victim = hand;
        hand = (hand+1) % NUM_FRAMES;
        return victim;
    }
};

class EscPager : public FifoPager {
private:
    static const unsigned int RESET_CYCLE = 50;
    unsigned long last_reset;
public:
    EscPager(bool OPTION_a) : FifoPager(OPTION_a), last_reset(-1) {}
    
    unsigned int select_victim_frame() {
        vector<int> classes(4, -1);
        unsigned int start = hand;
        unsigned int counter  = 0;
        bool reset = INSTR_COUNT-last_reset >= RESET_CYCLE;
        do {
            counter++;
            PTE& pte_rv = reversed_map(hand);
            int level = pte_rv.referenced*2 + pte_rv.modified;
            if (classes[level] == -1) {
                classes[level] = hand;
            }
            if (level == 0 && !reset) { break; }
            if (reset) { pte_rv.referenced = false; }
            hand = (hand+1) % NUM_FRAMES;
        } while (hand != start);
        if (reset) { last_reset = INSTR_COUNT; }
        
        auto victim = find_if(classes.begin(), classes.end(), [](int& x){
            return x != -1;
        });
        
        if (OPTION_a) {
            cout << "ASELECT: hand=" << setw(2) << start
                 << " " << setw(1) << reset
                 << " | " << setw(1) << (victim-classes.begin())
                 << " " << setw(2) << *victim
                 << " " << setw(2) << counter << setw(0) << endl;
        }
        
        hand = (*victim+1) % NUM_FRAMES;
        return *victim;
    }
};

class AgingPager : public FifoPager {
public:
    AgingPager(bool OPTION_a) : FifoPager(OPTION_a) {}
    
    void age_operation(unsigned int frame) {
        frame_table[frame].age = 0;
    }
    
    unsigned int select_victim_frame() {
        unsigned int start = hand;
        unsigned int min = hand;
        do {
            PTE& pte_rv = reversed_map(hand);
            frame_table[hand].age >>= 1;
            if (pte_rv.referenced) {
                frame_table[hand].age |= 0x80000000;
                pte_rv.referenced = false;
            }
            min = frame_table[hand].age<frame_table[min].age ? hand : min;
            hand = (hand+1) % NUM_FRAMES;
        } while (hand != start);
        
        if (OPTION_a) {
            cout << "ASELECT " << start
                 << "-" << (hand-1) % NUM_FRAMES << " | ";
            do {
                cout << start << ":"
                     << hex << frame_table[start].age << dec << " ";
                start = (start+1) % NUM_FRAMES;
            } while (start != hand);
            cout << "| " << min << endl;
        }
        
        hand = (min+1) % NUM_FRAMES;
        return min;
    }
};

class WorkingSetPager : public FifoPager {
private:
    static const unsigned int TAU = 49;
    vector<unsigned long> last_used;
public:
    WorkingSetPager(bool OPTION_a) : FifoPager(OPTION_a), last_used(NUM_FRAMES) {}
    
    void age_operation(unsigned int frame) {
        last_used[frame] = INSTR_COUNT;
    }
    
    unsigned int select_victim_frame() {
        unsigned int start = hand;
        unsigned int oldest = hand;
        stringstream ss;
        do {
            PTE& pte_rv = reversed_map(hand);
            if (OPTION_a) {
                ss << hand << "(" << pte_rv.referenced
                   << " " << frame_table[hand].pid_rv
                   << ":" << frame_table[hand].vpage_rv
                   << " " << last_used[hand] << ") ";
            }
            if (pte_rv.referenced) {
                last_used[hand] = INSTR_COUNT;
                pte_rv.referenced = false;
            }
            else if (INSTR_COUNT-last_used[hand] > TAU) {
                if (OPTION_a) {
                    ss << "STOP(" << (hand < start ?
                                      hand-start+NUM_FRAMES+1 :
                                      hand-start+1)  << ") ";
                }
                oldest = hand;
                break;
            }
            oldest = last_used[hand]<last_used[oldest] ? hand : oldest;
            hand = (hand+1) % NUM_FRAMES;
        } while (hand != start);
        
        if (OPTION_a) {
            cout << "ASELECT " << start
                 << "-" << (start-1) % NUM_FRAMES << " | "
                 << ss.str() << "| " << oldest << endl;
        }
        
        hand = (oldest+1) % NUM_FRAMES;
        return oldest;
    }
};

class RFile {
private:
    vector<int> randvals;   /// Store the  values from rfile
    size_t ofs;             /// Current offset in `randvals`
public:
    RFile(const string path) : ofs(0) {
        ifstream ifs(path);
        int n;
        ifs >> n;
        while(ifs >> n) {
            randvals.push_back(n);
        }
    }
    
    /**
     * Get a random integer [0, `bound`-1] inclusive using values `randvals` at location `ofs`
     *
     * @param[in] bound the upper bound of the random integer
     * @return a random integer
     */
    unsigned int randInt(const unsigned int bound) {
        ofs = ofs % randvals.size();
        return randvals[ofs++] % bound;
    }
};

class RandomPager : public Pager {
private:
    RFile rfile;
public:
    RandomPager(bool OPTION_a, string rfile) : Pager(OPTION_a), rfile(rfile) {}
    
    unsigned int select_victim_frame() {
        return rfile.randInt(NUM_FRAMES);
    }
};

ostream& operator << (ostream& os, const vector<Frame>& frame_table) {
    for (size_t i = 0; i < frame_table.size(); i++) {
        const Frame& frame = frame_table[i];
        if (frame.mapped) {
            os << frame.pid_rv << ":" << frame.vpage_rv << " ";
        } else {
            os << "* ";
        }
    }
    return os;
}

ostream& operator << (ostream& os, const vector<PTE>& page_table) {
    for (size_t i = 0; i < page_table.size(); i++) {
        const PTE& pte = page_table[i];
        if (pte.present) {
            os << i << ":"
               << (pte.referenced ? "R" : "-")
               << (pte.modified ? "M" : "-")
               << (pte.paged_out ? "S " : "- ");
        }
        else {
            os << (pte.paged_out ? "# " : "* ");
        }
    }
    return os;
}

ostream& operator << (ostream& os, const Process& process) {
    os << " U=" << process.unmaps
       << " M=" << process.maps
       << " I=" << process.ins
       << " O=" << process.outs
       << " FI=" << process.fins
       << " FO=" << process.fouts
       << " Z=" << process.zeros
       << " SV=" << process.segv
       << " SP=" << process.segprot;
    return os;
}

class InstructionLoader {
private:
    ifstream infile_;
    stringstream line_;

    bool get_next_valid_line() {
        string line;
        while (getline(infile_, line)) {
            if (line[0] == '#') continue;
            line_.clear();
            line_.str(line);
            return true;
        }
        return false;
    }
public:
    InstructionLoader(string filename) {
        infile_.open(filename);
    }

    void initialize_process() {
        if (!get_next_valid_line()) {
            throw "Number of process expected.";
        }
        int num_process;
        line_ >> num_process;
        while (num_process-- > 0) {
            mmu::process_pool.push_back(mmu::Process());
            if (!get_next_valid_line()) {
                throw "Number of VMA expected.";
            }
            int num_vma;
            line_ >> num_vma;
            while (num_vma-- > 0) {
                if (!get_next_valid_line()) {
                    throw "VMA expected.";
                }
                unsigned int start_vpage, end_vpage;
                bool write_protected, file_mapped;
                line_ >> start_vpage
                      >> end_vpage
                      >> write_protected
                      >> file_mapped;
                mmu::VMA vma{start_vpage, end_vpage, write_protected, file_mapped};
                mmu::process_pool.back().vmas.push_back(vma);
            }
        }
    }

    bool get_next_instruction(char& operation, int& vpage) {
        if (get_next_valid_line()) {
            line_ >> operation >> vpage;
            return true;
        }
        return false;
    }
};
}

#endif /* mmu_hpp */
