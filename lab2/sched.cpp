#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <string>
#include <set>
#include <unistd.h>
#include "sched.hpp"

using namespace std;
using namespace sched;

// Program Options
bool VERBOSE     = false;   // -v
bool SHOW_EVENTS = false;   // -e
bool SHOW_SCHED  = false;   // -t

// Other Global
int         sched::TOTAL_IO         = 0;        /// @see sched::TOTAL_IO
int         sched::EVENT_COUNTER    = 0;        /// @see sched::EVENT_COUNTER
int         CURRENT_TIME            = 0;        /// Current Time
int         IO_END_TIME             = 0;        /// Last Timestamp When an IO Finish
int         DONE_EVT                = 0;        /// Num of Done Events
bool        CALL_SCHEDULER          = false;    /// Flag For Calling Scheduler
Process*    CURRENT_RUNNING_PROCESS = nullptr;  /// Current Running Process

class RFile {
private:
    vector<int> randvals;   /// Store the  values from rfile
    int ofs;                /// Current offset in `randvals`
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
     * Get a random integer [1, `bound`] inclusive using values `randvals` at location `ofs`
     *
     * @param[in] bound the upper bound of the random integer
     * @return a random integer
     */
    int randInt(const int bound) {
        if (ofs >= randvals.size()) ofs = 0;
        return 1 + (randvals[ofs++] % bound);
    }
};

class DES {
private:
    set<Event*, EvtLess> eventQ;    /// Event Queue
public:
    /**
     * Initialize event queue from an input file
     *
     * @param[in] filepath the path of input file
     * @param[in] rand a `RFile` object for getting random values
     * @param[in] maxprio max possible static priority
     */
    void init_event_queue(string filepath,  RFile& rand, int maxprio=4) {
        if (SHOW_EVENTS) cout << "ShowEventQ: ";
        ifstream ifs(filepath);
        int pid = 0;
        int AT, TC, CB, IO;
        while (ifs >> AT >> TC >> CB >> IO) {
            if (SHOW_EVENTS) cout << " " << AT << ":" << pid << " ";
            int static_prio = rand.randInt(maxprio);
            Process *proc = new Process(pid++, AT, TC, CB, IO, static_prio);
            Event *event = new Event(proc,
                                     AT,
                                     STATE::CREATED,
                                     STATE::READY,
                                     TRANSITION::TRANS_TO_READY);
            eventQ.insert(event);
        }
        if (SHOW_EVENTS) cout << endl;
        ifs.close();
    }
    
    /**
     * Get the next `Event` pointer in the `eventQ`
     *
     * @return the next `Event` pointer in the `eventQ`
     */
    Event* get_event() {
        if (eventQ.empty()) return nullptr;
        Event* evt = *eventQ.begin();
        eventQ.erase(eventQ.begin());
        return evt;
    }
    
    /**
     * Put an `Event` pointer into the `eventQ`
     *
     * @param[in] evt the `Event` pointer for the operation
     */
    void put_event(Event* evt) {
        eventQ.insert(evt);
        
        if (SHOW_EVENTS) {
            cout << "  AddEvent(" << *evt << "):";
            stringstream after;
            for (Event* e : eventQ) {
                after << "  " << *e;
                if (e->eid != evt->eid) {
                    cout << "  " << *e;
                }
            }
            cout << " ==> " << after.str() << endl;
        }
    }
    
    /**
     * Remove  an element from the `eventQ`
     *
     * @param[in] it an iterator of `eventQ` for the operation
     */
    void rm_event(decltype(eventQ.begin()) it) {
        if (VERBOSE) {
            cout << "RemoveEvent(" << (*it)->evtProcess->pid << "):";
            stringstream after;
            for (Event* e : eventQ) {
                if (e->eid != (*it)->eid) {
                    after << " " << *e;
                }
                cout << "  " << e->str(false);
            }
            cout << " ==> " << after.str() << endl;
        }
        eventQ.erase(it);
    }
    
    /**
     * Get the event timestamp of the next event in `eventQ`
     *
     * @return the event timestamp of the next event in `eventQ`
     */
    int get_next_event_time() const {
        if (eventQ.empty()) return -1;
        return (*eventQ.begin())->evtTimestamp;
    }
    
    /**
     * Get the pending event of `CURRENT_RUNNING_PROCESS`
     *
     * @return the iterator of the pending event in the `eventQ`
     */
    decltype(eventQ.begin()) pendingEvent() {
        auto it = find_if(eventQ.begin(), eventQ.end(), [](Event* e) {
            return e->evtProcess->pid == CURRENT_RUNNING_PROCESS->pid;
        });
        return it;
    }
};

/**
 * Update `TOTAL_IO` according to `CURRENT_TIME`, `IO_END_TIME` and `io_burst`
 *
 * @param[in] io_burst the IO burst happend at current time
 */
void update_total_io(int io_burst) {
    int curEndtime = CURRENT_TIME + io_burst;
    if (IO_END_TIME < curEndtime) {
        TOTAL_IO += curEndtime - max(CURRENT_TIME, IO_END_TIME);
        IO_END_TIME = curEndtime;
    }
}

/**
 * Simulate the scheduling the Process using Discrete Event Simulation (DES)
 *
 * @param[in] des the DES layer of the simulation
 * @param[in] sched the scheduler
 * @param[in] rand the `RFile` object to generate random integer
 */
void simulation(DES& des, Scheduler& sched, RFile& rand) {
    Event *evt;
    while( (evt = des.get_event()) ) {
        Process *proc = evt->evtProcess;
        CURRENT_TIME = evt->evtTimestamp;
        int timeInPrevState = CURRENT_TIME - proc->stateTs;
        proc->stateTs = CURRENT_TIME;
        proc->stateDE = DONE_EVT;
        
        switch(evt->transition) {
            case TRANS_TO_READY: {
                if (VERBOSE) {
                    cout << CURRENT_TIME << " "
                         << proc->pid << " "
                         << timeInPrevState << ": "
                         << STATE_TO_STR[ evt->oldState ] << " -> "
                         << STATE_TO_STR[ evt->newState ] << endl;
                }
                proc->remBurst = 0;
                proc->dynamicPrio = proc->staticPrio - 1;
                sched.add_process(proc);
                // priority preemption check
                if (sched.prioPreempt && CURRENT_RUNNING_PROCESS) {
                    bool prioTest = proc->dynamicPrio >
                                    CURRENT_RUNNING_PROCESS->dynamicPrio;
                    auto pendEvt = des.pendingEvent();
                    int pendEvtTime = (*pendEvt)->evtTimestamp;
                    bool prioPrempt = prioTest &&
                                     (pendEvtTime != CURRENT_TIME);
                    if (VERBOSE) {
                        cout << "---> PRIO preemption "
                             << CURRENT_RUNNING_PROCESS->pid
                             << " by " << proc->pid
                             << " ? " << prioTest
                             << " TS=" << pendEvtTime
                             << " now=" << CURRENT_TIME
                             << ") --> " << (prioPrempt ? "YES" : "NO") << endl;
                    }
                    if (prioPrempt) {
                        CURRENT_RUNNING_PROCESS->rem += pendEvtTime-CURRENT_TIME;
                        CURRENT_RUNNING_PROCESS->remBurst += pendEvtTime-CURRENT_TIME;
                        des.rm_event(pendEvt);
                        Event *e = new Event(CURRENT_RUNNING_PROCESS,
                                             CURRENT_TIME,
                                             STATE::RUNNING,
                                             STATE::READY,
                                             TRANSITION::TRANS_TO_PREEMPT);
                        des.put_event(e);
                    }
                }
                CALL_SCHEDULER = true;
                break;
            }
            case TRANS_TO_RUN: {
                int cpu_burst = CURRENT_RUNNING_PROCESS->remBurst > 0 ?
                                CURRENT_RUNNING_PROCESS->remBurst :
                                rand.randInt(CURRENT_RUNNING_PROCESS->cpuBurst);
                cpu_burst = min(cpu_burst, CURRENT_RUNNING_PROCESS->rem);
                if (VERBOSE) {
                    cout << CURRENT_TIME << " "
                         << proc->pid << " "
                         << timeInPrevState << ": "
                         << STATE_TO_STR[ evt->oldState ] << " -> "
                         << STATE_TO_STR[ evt->newState ]
                         << " cb=" << to_string(cpu_burst)
                         << " rem=" << to_string(proc->rem)
                         << " prio=" << to_string(proc->dynamicPrio) << endl;
                }
                // quantum preemption check
                if (cpu_burst > sched.quantum) {
                    proc->rem -= sched.quantum;
                    proc->remBurst = cpu_burst - sched.quantum;
                    int end_time = CURRENT_TIME + sched.quantum;
                    Event *e = new Event(proc,
                                         end_time,
                                         STATE::RUNNING,
                                         STATE::READY,
                                         TRANSITION::TRANS_TO_PREEMPT);
                    des.put_event(e);
                } else {
                    proc->rem -= cpu_burst;
                    proc->remBurst = 0;
                    int end_time = CURRENT_TIME + cpu_burst;
                    Event *e = new Event(proc,
                                         end_time,
                                         STATE::RUNNING,
                                         STATE::BLOCK,
                                         TRANSITION::TRANS_TO_BLOCK);
                    des.put_event(e);
                }
                break;
            }
            case TRANS_TO_BLOCK: {
                int io_burst = proc->rem > 0 ?
                               rand.randInt(proc->ioBurst) : 0;
                proc->ioTime += io_burst;
                update_total_io(io_burst);
                if (VERBOSE) {
                    cout << CURRENT_TIME << " "
                         << proc->pid << " "
                         << timeInPrevState << ": ";
                    if (proc->rem) {
                        cout << STATE_TO_STR[ evt->oldState ] << " -> "
                             << STATE_TO_STR[ evt->newState ]
                             << " ib=" + to_string(io_burst)
                             << " rem=" + to_string(proc->rem) << endl;
                    } else {
                        cout << "Done" << endl;
                    }
                }
                if (!proc->rem) {
                    proc->finishTime = CURRENT_TIME;
                    sched.done_process(proc);
                } else {
                    int end_time = CURRENT_TIME + io_burst;
                    Event *e = new Event(proc,
                                         end_time,
                                         STATE::BLOCK,
                                         STATE::READY,
                                         TRANSITION::TRANS_TO_READY);
                    des.put_event(e);
                }
                CURRENT_RUNNING_PROCESS = nullptr;
                CALL_SCHEDULER = true;
                break;
            }
            case TRANS_TO_PREEMPT: {
                if (VERBOSE) {
                    cout << CURRENT_TIME << " "
                         << proc->pid << " "
                         << timeInPrevState << ": "
                         << STATE_TO_STR[ evt->oldState ] << " -> "
                         << STATE_TO_STR[ evt->newState ]
                         << " cb=" << to_string(proc->remBurst)
                         << " rem=" << to_string(proc->rem)
                         << " prio=" << to_string(proc->dynamicPrio) << endl;
                }
                sched.decay(proc);
                sched.add_process(proc);
                CURRENT_RUNNING_PROCESS = nullptr;
                CALL_SCHEDULER = true;
                break;
            }
        }
        delete evt;
        evt = nullptr;
        DONE_EVT++;
        
        if (CALL_SCHEDULER) {
            if (des.get_next_event_time() == CURRENT_TIME) {
                continue;
            }
            CALL_SCHEDULER = false;
            if (CURRENT_RUNNING_PROCESS == nullptr) {
                if (SHOW_SCHED) {
                    sched.print_ready_queue();
                }
                CURRENT_RUNNING_PROCESS = sched.get_next_process();
                if (CURRENT_RUNNING_PROCESS == nullptr) {
                    continue;
                }
                CURRENT_RUNNING_PROCESS->waitTime +=
                                CURRENT_TIME - CURRENT_RUNNING_PROCESS->stateTs;
                Event *e = new Event(CURRENT_RUNNING_PROCESS,
                                     CURRENT_TIME,
                                     STATE::READY,
                                     STATE::RUNNING,
                                     TRANSITION::TRANS_TO_RUN);
                des.put_event(e);
            }
        }
    }
}

int main(int argc, char * const argv[]) {
    // get the command line option
    opterr = 0;
    string svalue;
    int c;
    while ((c = getopt(argc, argv, "vtes:")) != -1) {
        switch (c) {
            case 'v':
                VERBOSE = true;
                break;
            case 't':
                SHOW_SCHED = true;
                break;
            case 'e':
                SHOW_EVENTS = true;
                break;
            case 's':
                svalue = optarg;
                break;
            case '?':
                if (optopt == 's')
                    cerr << "Option -" << char(optopt)
                         << "requires an argument." << endl;
                else if (isprint (optopt))
                    cerr << "Unknown option `-" << char(optopt)
                         << "'." << endl;
                else
                    cerr << "Unknown option character `\\x"
                         << hex << optopt << "'." << endl;
            default:
                return 1;
        }
    }
    argc -= optind;
    argv += optind;
    
    if (argc < 2) {
        cerr << "Both input file and rfile are required." << endl;
        return 1;
    }
    
    SHOW_SCHED &= VERBOSE;
    SHOW_EVENTS &= VERBOSE;
    const string input = argv[0];
    const string rfile = argv[1];
    int quantum = 10000;
    int maxprio = 4;
    
    // initialize `sched`, `quantum` and `maxprio` according to -s option
    Scheduler* sched = nullptr;
    switch(svalue[0]) {
        case 'F': {
            sched = new SchedulerFR;
            break;
        }
        case 'L': {
            sched = new SchedulerL;
            break;
        }
        case 'S': {
            sched = new SchedulerS;
            break;
        }
        case 'R': {
            try {
                auto colon = svalue.find(':');
                if (colon == string::npos) colon = svalue.size();
                quantum = stoi( svalue.substr(1, colon-1) );
                sched = new SchedulerFR(quantum);
                break;
            } catch (const exception& e) {
                cerr << "Invalid scheduler param: <" << svalue << ">." << endl;
                return 1;
            }
        }
        case 'P': {
            try {
                auto colon = svalue.find(':');
                if (colon == string::npos) {
                    colon = svalue.size();
                } else {
                    maxprio = stoi( svalue.substr(colon+1, svalue.size()-colon-1) );
                }
                quantum = stoi( svalue.substr(1, colon-1) );
                sched = new SchedulerPE(quantum, false, maxprio);
                break;
            } catch (const exception& e) {
                cerr << "Invalid scheduler param: <" << svalue << ">." << endl;
                return 1;
            }
        }
        case 'E': {
            try {
                auto colon = svalue.find(':');
                if (colon == string::npos) {
                    colon = svalue.size();
                } else {
                    maxprio = stoi( svalue.substr(colon+1, svalue.size()-colon-1) );
                }
                quantum = stoi( svalue.substr(1, colon-1) );
                sched = new SchedulerPE(quantum, true, maxprio);
                break;
            } catch (const exception& e) {
                cerr << "Invalid scheduler param: <" << svalue << ">." << endl;
                return 1;
            }
        }
        default:
            cerr << "Unknown scheduler type {FLSRPE} -" << svalue[0]
                 <<  "." << endl;
            return 1;
    }
    
    RFile rand(rfile);
    DES des;

    des.init_event_queue(input, rand, maxprio);
    simulation(des, *sched, rand);
    sched->statistics();
    
    delete sched;
    
    return 0;
}
