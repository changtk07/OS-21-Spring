#include <iostream>
#include <string>
#include <unistd.h>
#include "mmu.hpp"

using namespace std;
using namespace mmu;

bool OPTION_O = false;
bool OPTION_P = false;
bool OPTION_F = false;
bool OPTION_S = false;
bool OPTION_a = false;
bool OPTION_f = false;
bool OPTION_x = false;
bool OPTION_y = false;

Pager* pager = nullptr;

unsigned int allocate_frame(unsigned int vpage) {
    unsigned int f;
    if (!free_frames.empty()) {
        f = free_frames.front();
        free_frames.pop_front();
    }
    else {
        f = ::pager->select_victim_frame();
        unsigned int pid_rv = frame_table[f].pid_rv;
        unsigned int vpage_rv = frame_table[f].vpage_rv;
        if (OPTION_O) {
            cout << " UNMAP " << pid_rv << ":" << vpage_rv << endl;
        }
        Process& process_rv = process_pool[pid_rv];
        process_rv.unmaps++;
        COST += COST_TABLE::UNMAPS;
        PTE& pte_rv = process_pool[pid_rv].page_table[vpage_rv];
        pte_rv.present = false;
        if (pte_rv.modified) {
            pte_rv.paged_out = !pte_rv.file_mapped;
            if (OPTION_O) {
                cout << (pte_rv.file_mapped ? " FOUT" : " OUT") << endl;
            }
            (pte_rv.file_mapped ? process_rv.fouts : process_rv.outs)++;
            COST += (pte_rv.file_mapped ? COST_TABLE::FOUTS
                                          : COST_TABLE::OUTS);
        }
    }
    frame_table[f].pid_rv = CURRENT_PID;
    frame_table[f].vpage_rv = vpage;
    frame_table[f].mapped = true;
    return f;
}

void page_fault_handler(PTE& pte, unsigned int vpage) {
    // determine that the `vpage` can be accessed
    if (!pte.vma_checked) {
        pte.vma_checked = true;
        for (VMA& vma : current_process().vmas) {
            if (vpage >= vma.start_vpage && vpage <= vma.end_vpage) {
                pte.vma_valid = true;
                pte.file_mapped = vma.file_mapped;
                pte.write_protect = vma.write_protect;
                break;
            }
        }
    }
    if (pte.vma_valid) {
        pte.present = true;
        pte.frame = allocate_frame(vpage);
        if (pte.paged_out) {
            if (OPTION_O) {
                cout << " IN" << endl;
            }
            current_process().ins++;
            COST += COST_TABLE::INS;
        }
        else if (pte.file_mapped) {
            if (OPTION_O) {
                cout << " FIN" << endl;
            }
            current_process().fins++;
            COST += COST_TABLE::FINS;
        }
        else {
            if (OPTION_O) {
                cout << " ZERO" << endl;
            }
            current_process().zeros++;
            COST += COST_TABLE::ZEROS;
        }
        if (OPTION_O) {
            cout << " MAP " << pte.frame << endl;
        }
        current_process().maps++;
        COST += COST_TABLE::MAPS;
        ::pager->age_operation(pte.frame);
    }
    else {
        if (OPTION_O) {
            cout << " SEGV" << endl;
        }
        current_process().segv++;
        COST += COST_TABLE::SEGV;
    }
}

void simulation(InstructionLoader& loader) {
    loader.initialize_process();
    char operation;
    int operand;
    while ( loader.get_next_instruction(operation, operand) ) {
        if (OPTION_O) {
            cout << INSTR_COUNT << ": ==> "
                 << operation << " "
                 << operand << endl;
        }
        switch (operation) {
            case 'w':
            case 'r': {
                PTE& pte = current_process().page_table[operand];
                pte.referenced = true;
                COST += COST_TABLE::READ_WRITE;
                if (!pte.present) {
                    pte.modified = false;
                    page_fault_handler(pte, operand);
                }
                if (operation == 'w' && pte.write_protect) {
                    if (OPTION_O) {
                        cout << " SEGPROT" << endl;
                    }
                    current_process().segprot++;
                    COST += COST_TABLE::SEGPROT;
                }
                else {
                    pte.modified |= operation=='w';
                }
                
                if (OPTION_y && pte.vma_valid) {
                    for (size_t i = 0; i < process_pool.size(); i++) {
                        Process& proc = process_pool[i];
                        cout << "PT[" << i << "]: " << proc.page_table << endl;
                    }
                }
                else if (OPTION_x && pte.vma_valid) {
                    cout << "PT[" << CURRENT_PID << "]: "
                         << current_process().page_table << endl;
                }
                if (OPTION_f && pte.vma_valid) {
                    cout << "FT: " << frame_table << endl;
                }
                break;
            }
            case 'c': {
                CTX_SWITCHES++;
                CURRENT_PID = operand;
                COST += COST_TABLE::SWITCHES;
                break;
            }
            case 'e': {
                cout << "EXIT current process " << operand << endl;
                PROCESS_EXITS++;
                COST += COST_TABLE::EXITS;
                Process& proc = process_pool[operand];
                for (size_t i = 0; i < proc.page_table.size(); i++) {
                    PTE& pte = proc.page_table[i];
                    pte.paged_out = false;
                    if (pte.present) {
                        pte.present = false;
                        if (OPTION_O) {
                            cout << " UNMAP " << operand << ":" << i << endl;
                        }
                        proc.unmaps++;
                        COST += COST_TABLE::UNMAPS;
                        frame_table[pte.frame].mapped = false;
                        free_frames.push_back(pte.frame);
                        if (pte.file_mapped && pte.modified) {
                            if (OPTION_O) {
                                cout << " FOUT" << endl;
                            }
                            proc.fouts++;
                            COST += COST_TABLE::FOUTS;
                        }
                    }
                }
            }
        }
        INSTR_COUNT++;
    }
}

bool cmd_option(int& argc, char* const argv[], char& algo) {
    opterr = 0;
    int o;
    while ((o = getopt(argc, argv, "f:a:o:")) != -1) {
        switch (o) {
            case 'f':
                NUM_FRAMES = stoi(optarg);
                break;
            case 'a':
                algo = string(optarg)[0];
                break;
            case 'o': {
                string opt = string(optarg);
                for (char& c : opt) {
                    switch (c) {
                        case 'O':
                            ::OPTION_O = true;
                            break;
                        case 'P':
                            ::OPTION_P = true;
                            break;
                        case 'F':
                            ::OPTION_F = true;
                            break;
                        case 'S':
                            ::OPTION_S = true;
                            break;
                        case 'a':
                            ::OPTION_a = true;
                            break;
                        case 'f':
                            ::OPTION_f = true;
                            break;
                        case 'x':
                            ::OPTION_x = true;
                            break;
                        case 'y':
                            ::OPTION_y = true;
                            break;
                        default:
                            cerr << "Unknown output option: `" << c
                                 << "'." << endl;
                            return true;
                    }
                }
                break;
            }
            case '?':
                if (optopt == 'f' || optopt == 'a' || optopt == 'o')
                    cerr << "Option -" << char(optopt)
                         << "requires an argument." << endl;
                else if (isprint (optopt))
                    cerr << "Unknown option `-" << char(optopt)
                         << "'." << endl;
                else
                    cerr << "Unknown option character `\\x"
                         << hex << optopt << "'." << endl;
            default:
                return true;
        }
    }
    
    return false;
}

int main(int argc, char* const argv[]) {
    char algo = 'f';
    if (cmd_option(argc, argv, algo)) {
        return 1;
    }
    argc -= optind;
    argv += optind;
    if (argc < 2) {
        cerr << "Both input file and rfile are required." << endl;
        return 1;
    }
    
    InstructionLoader loader(argv[0]);

    for (unsigned int i = 0; i < NUM_FRAMES; i++) {
        frame_table.push_back(Frame());
        free_frames.push_back(i);
    }
    
    switch (algo) {
        case 'f':
            ::pager = new FifoPager(OPTION_a);
            break;
        case 'c':
            ::pager = new ClockPager(OPTION_a);
            break;
        case 'a':
            ::pager = new AgingPager(OPTION_a);
            break;
        case 'e':
            ::pager = new EscPager(OPTION_a);
            break;
        case 'w':
            ::pager = new WorkingSetPager(OPTION_a);
            break;
        case 'r':
            ::pager = new RandomPager(OPTION_a, argv[1]);
            break;
        default:
            cerr << "Unknown paging alogrithm: `" << algo << "'." << endl;
            return 1;
    }
    
    simulation(loader);
    delete ::pager;
    
    if (OPTION_P) {
        for (size_t i = 0; i < process_pool.size(); i++) {
            Process& proc = process_pool[i];
            cout << "PT[" << i << "]: " << proc.page_table << endl;
        }
    }
    
    if (OPTION_F) {
        cout << "FT: " << frame_table << endl;
    }
    
    if (OPTION_S) {
        for (size_t i = 0; i < process_pool.size(); i++) {
            Process& proc = process_pool[i];
            cout << "PROC[" << i << "]:" << proc << endl;
        }

        cout << "TOTALCOST " << INSTR_COUNT << " "
                             << CTX_SWITCHES << " "
                             << PROCESS_EXITS << " "
                             << COST << " "
                             << sizeof(PTE) << endl;
    }

    return 0;
}
