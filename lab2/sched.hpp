#pragma once
#ifndef sched_hpp
#define sched_hpp
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <string>
#include <vector>
#include <queue>
#include <stack>

namespace sched {

using namespace std;

enum STATE {
    CREATED,
    READY,
    RUNNING,
    BLOCK
};
enum TRANSITION {
    TRANS_TO_READY,
    TRANS_TO_RUN,
    TRANS_TO_BLOCK,
    TRANS_TO_PREEMPT
};

// enum to string Dictionary
const string STATE_TO_STR[] { "CREATED", "READY", "RUNNG", "BLOCK" };
const string TRANSITION_TO_STR[] { "READY", "RUNNG", "BLOCK", "PREEMPT" };

// Other Global
extern int EVENT_COUNTER;   /// Event Counter
extern int TOTAL_IO;        /// Global Total IO Time

struct Process {
    const int pid;          /// Process Unique ID
    const int arriveTime;   /// Arrive Time
    const int totalCpu;     /// Total CPU Time
    const int cpuBurst;     /// Max CPU Burst
    const int ioBurst;      /// Max IO Burst
    const int staticPrio;   /// Static Priority
    
    int stateDE;            /// Num of Done Events When Trans To Curr State(used to break ties for SRTF)
    int stateTs;            /// Timestamp of Current State
    int rem;                /// Remain CPU Time
    int dynamicPrio;        /// Dynamic Priority
    int waitTime;           /// Total Wait Time In Ready Queue
    int ioTime;             /// Total IO Time
    int finishTime;         /// Finish Timestamp
    int remBurst;           /// Remain CPU Burst
    
    Process(int pid,
            int arriveTime,
            int totalCpu,
            int cpuBurst,
            int ioBurst,
            int staticPrio) :
    pid(pid),
    arriveTime(arriveTime),
    totalCpu(totalCpu),
    cpuBurst(cpuBurst),
    ioBurst(ioBurst),
    staticPrio(staticPrio),
    stateTs(arriveTime),
    rem(totalCpu),
    stateDE(-1),
    dynamicPrio(staticPrio-1),
    waitTime(0),
    ioTime(0),
    remBurst(0) {}
    
    friend ostream& operator << (ostream& os, const Process& proc);
};

ostream& operator << (ostream& os, const Process& proc) {
    os << proc.pid << ":" << proc.stateTs;
    return os;
}

struct Event {
    const int eid;                  /// Event Unique ID
    Process *evtProcess;            /// Event Process
    const int evtTimestamp;         /// Event Timestamp
    const STATE oldState;           /// Process Old State
    const STATE newState;           /// Process New State
    const TRANSITION transition;    /// Process State Transition
    
    Event(Process *evtProcess,
          int evtTimeStamp,
          STATE oldState,
          STATE newState,
          TRANSITION transition) :
    eid(EVENT_COUNTER++),
    evtProcess(evtProcess),
    evtTimestamp(evtTimeStamp),
    oldState(oldState),
    newState(newState),
    transition(transition) {}
    
    /**
     * Convert Event struct to std::string
     *
     * @param[in] trans the flag for including state trasition
     * @return convted std::string
     */
    string str(bool trans) const {
        return to_string(evtTimestamp) + ":" +
               to_string(evtProcess->pid) +
               (trans ? ":"+TRANSITION_TO_STR[transition] : "");
    }
    
    friend ostream& operator << (ostream& os, const Event& evt);
};

ostream& operator << (ostream& os, const Event& evt) {
    os << evt.str(true);
    return os;
}


struct ProcGreater {
    /**
     * Ccompae two `Process` pointers according to objects' `pid` they point to
     *
     * @param[in] lhs left hand side `Process` pointer
     * @param[in] rhs right hand side `Process` pointer
     * @return true if `lhs` has greater `pid`, else false
     */
    bool operator () (Process* lhs, Process* rhs) const {
        return lhs->pid > rhs->pid;
    }
};

struct ProcRemGreater {
    /**
     * Ccompae two `Process` pointers according to objects' `rem` they point to
     * and uses `stateDE` to break ties
     *
     * @param[in] lhs left hand side `Process` pointer
     * @param[in] rhs right hand side `Process` pointer
     * @return true if `lhs` has greater `rem`, if less then false. If they are equal,
     *         then true for greater `stateDE`
     */
    bool operator () (Process* lhs, Process* rhs) const {
        if (lhs->rem > rhs->rem) return true;
        else if (lhs->rem < rhs->rem) return false;
        else return lhs->stateDE > rhs->stateDE;
    }
};

struct EvtLess {
    /**
     * Ccompae two `Event` pointers according to objects' `evtTimestamp` they point to
     * and uses `eid` to break ties
     *
     * @param[in] lhs left hand side `Event` pointer
     * @param[in] rhs right hand side `Event` pointer
     * @return true if `lhs` has less `evtTimestamp`, if greater then false. If they are equal,
     *         then true for less `eid`
     */
    bool operator () (Event* lhs, Event* rhs) const {
        if (lhs->evtTimestamp < rhs->evtTimestamp) return true;
        else if (lhs->evtTimestamp > rhs->evtTimestamp) return false;
        else return lhs->eid < rhs->eid;
    }
};

/**
 * Scheduler Base Class
 */
class Scheduler {
protected:
    priority_queue<Process*, vector<Process*>, ProcGreater> done;   /// Queue For Done Process
    const string type;  /// The Type of Scheduler
    
    /**
     * Return the first element of `Container` template class type that use `top()`
     * to access first element
     *
     * @tparam Container type of the container class
     * @param[in] c an object of `Container` template class type for the operation
     * @return the first element in `c`
     */
    template<class Container>
    static auto next(Container& c) -> decltype(c.top()) {
        return c.top();
    }
    /**
     * Return the first element of `Container` template class type that use `front()`
     * to access first element
     *
     * @tparam Container type of the container class
     * @param[in] c an object of `Container` template class type for the operation
     * @return the first element in `c`
     */
    template<class Container>
    static auto next(Container& c) -> decltype(c.front()) {
        return c.front();
    }
    /**
     * Print the element in a `Container` template class type
     *
     * @tparam Container type of the container class
     * @param[in] readyQ an object of `Container` template class type for printing
     */
    template<class Container>
    static void print_ready_queue(Container readyQ) {
        cout << "SCHED (" << readyQ.size() << "):";
        while (!readyQ.empty()) {
            Process* p = next(readyQ);
            readyQ.pop();
            cout << "  " << *p;
        }
        cout << endl;
    }
public:
    const int quantum;      /// Quantum For Process Preemption
    const bool prioPreempt; /// Flag For Priority Preemption
    
    Scheduler(string type, int quantum, bool prioPreempt=false) :
        type(type), quantum(quantum), prioPreempt(prioPreempt) {}
    
    virtual ~Scheduler() {}
    
    /**
     * Add a process pointer to the ready queue
     *
     * @param[in] proc a `Process` pointer to for the operation
     */
    virtual void add_process(Process* proc) = 0;
    
    /**
     * Get the next `Process` pointer in the ready queue and
     * remove it from the ready queue. For PRIO and PREPRIO
     * schedulers, it will be applied to active queue
     *
     * @return the next `Process` pointer in the ready queue
     */
    virtual Process* get_next_process() = 0;
    
    /**
     * Print the element in the ready queue
     */
    virtual void print_ready_queue() = 0;
    
    /**
     * Decrement the dynamic priority of a `Process` for PREPRIO
     * scheduler and do nothing for other type of scheduler
     *
     * @param[in] proc a `Process` pointer for the operation
     */
    virtual void decay(Process* proc) = 0;
    
    /**
     * Put a `Process` pointer into done queue
     *
     * @param[in] proc a `Process` pointer for the operation
     */
    void done_process(Process* proc) {
        done.push(proc);
    }
    
    /**
     * Print the stastics of the scheduler
     */
    void statistics() {
        int FT = 0, TC = 0, WT = 0, TT = 0;
        int NP = static_cast<int>(done.size());
        cout << type << endl;
        while (!done.empty()) {
            Process* proc = done.top();
            done.pop();
            cout << setw(4) << setfill('0') << proc->pid << ": "
                 << setw(4) << setfill(' ') << proc->arriveTime << " "
                 << setw(4) << proc->totalCpu << " "
                 << setw(4) << proc->cpuBurst << " "
                 << setw(4) << proc->ioBurst << " "
                 << setw(1) << proc->staticPrio << " | "
                 << setw(5) << proc->finishTime << " "
                 << setw(5) << (proc->finishTime-proc->arriveTime) << " "
                 << setw(5) << proc->ioTime << " "
                 << setw(5) << proc->waitTime << endl;
            FT = max(FT, proc->finishTime);
            TC += proc->totalCpu;
            WT += proc->waitTime;
            TT += proc->finishTime - proc->arriveTime;
            delete proc;
        }

        cout << "SUM: " << FT << " "
             << fixed << setprecision(2) << ((double)TC/FT*100.) << " "
             << fixed << setprecision(2) << ((double)TOTAL_IO/FT*100.) << " "
             << fixed << setprecision(2) << ((double)TT/NP) << " "
             << fixed << setprecision(2) << ((double)WT/NP) << " "
             << fixed << setprecision(3) << (NP/((double)FT/100.)) << endl;
    }
};

/**
 * FCFS and RR Scheduler
 */
class SchedulerFR : public Scheduler {
private:
    queue<Process*> readyQ; /// Ready Queue
public:
    SchedulerFR() : Scheduler("FCFS", 10000) {}
    SchedulerFR(int quantum) : Scheduler("RR "+to_string(quantum), quantum) {}
    
    void decay(Process* proc) {}
    
    /**
     * @see Scheduler::add_process
     */
    void add_process(Process* proc) {
        readyQ.push(proc);
    }
    
    /**
     * @see Scheduler::get_next_process
     */
    Process* get_next_process() {
        if (readyQ.empty()) return nullptr;
        Process* p = readyQ.front();
        readyQ.pop();
        return p;
    }
    
    /**
     * @see Scheduler::print_ready_queue
     */
    void print_ready_queue() {
        Scheduler::print_ready_queue(readyQ);
    }
};

/**
 * LCFS Scheduler
 */
class SchedulerL : public Scheduler {
private:
    stack<Process*> readyQ; /// Ready Queue
public:
    SchedulerL() : Scheduler("LCFS", 10000) {}
    
    /**
     * @see Scheduler::decay
     */
    void decay(Process* proc) {}
    
    /**
     * @see Scheduler::add_process
     */
    void add_process(Process* proc) {
        readyQ.push(proc);
    }
    
    /**
     * @see Scheduler::get_next_process
     */
    Process* get_next_process() {
        if (readyQ.empty()) return nullptr;
        Process* p = readyQ.top();
        readyQ.pop();
        return p;
    }
    
    /**
     * @see Scheduler::print_ready_queue
     */
    void print_ready_queue() {
        Scheduler::print_ready_queue(readyQ);
    }
};

/**
 * SRTF Scheduler
 */
class SchedulerS : public Scheduler {
private:
    priority_queue<Process*, vector<Process*>, ProcRemGreater> readyQ;  /// Ready Queue
public:
    SchedulerS() : Scheduler("SRTF", 10000) {}
    
    /**
     * @see Scheduler::decay
     */
    void decay(Process* proc) {}
    
    /**
     * @see Scheduler::add_process
     */
    void add_process(Process* proc) {
        readyQ.push(proc);
    }
    
    /**
     * @see Scheduler::get_next_process
     */
    Process* get_next_process() {
        if (readyQ.empty()) return nullptr;
        Process* p = readyQ.top();
        readyQ.pop();
        return p;
    }
    
    /**
     * @see Scheduler::print_ready_queue
     */
    void print_ready_queue() {
        Scheduler::print_ready_queue(readyQ);
    }
};

/**
 * PRIO and PREPRIO Scheduler
 */
class SchedulerPE : public Scheduler {
protected:
    vector<queue<Process*>> active;     // Active Queue
    vector<queue<Process*>> expired;    // Expired Queue
    
    /**
     * Print a multi-level queue (vector of queues)
     *
     * @param[in] mlq a multi-level queue for printing
     */
    static void print_mlq(const vector<queue<Process*>>& mlq) {
        cout << "{ ";
        for (const queue<Process*>& q : mlq) {
            auto copy = q;
            cout << "[";
            if (!copy.empty()) {
                cout << copy.front()->pid;
                copy.pop();
                while (!copy.empty()) {
                    cout << "," << copy.front()->pid;
                    copy.pop();
                }
            }
            cout << "]";
        }
        cout << "} : ";
    }
public:
    SchedulerPE(int quantum, bool prioPreempt, int maxprio=4) :
    Scheduler((prioPreempt ? "PREPRIO " : "PRIO ") + to_string(quantum),
              quantum,
              prioPreempt)
    {
        active.resize(maxprio);
        expired.resize(maxprio);
    }
    
    /**
     * @see Scheduler::decay
     */
    void decay(Process* proc) {
        proc->dynamicPrio--;
    }
    
    /**
     * @see Scheduler::add_process
     */
    void add_process(Process* proc) {
        if (proc->dynamicPrio < 0) {
            proc->dynamicPrio = proc->staticPrio - 1;
            auto idx = expired.size() - proc->dynamicPrio - 1;
            expired[ idx ].push(proc);
        } else {
            auto idx = active.size() - proc->dynamicPrio - 1;
            active[ idx ].push(proc);
        }
    }
    
    /**
     * @see Scheduler::get_next_process
     */
    Process* get_next_process() {
        for (int i = 0; i < 2; i++) {
            auto it = find_if(active.begin(), active.end(),
                              [](queue<Process*>& q) { return !q.empty(); });
            if (it != active.end()) {
                Process* p = it->front();
                it->pop();
                return p;
            }
            // the built-in vector::swap actually swap the pointers
            active.swap(expired);
        }
        return nullptr;
    }
    
    /**
     * @see Scheduler::print_ready_queue
     */
    void print_ready_queue() {
        print_mlq(active);
        print_mlq(expired);
        cout << endl;
        auto it = find_if(active.begin(), active.end(),
                          [](queue<Process*>& q) { return !q.empty(); });
        if (it == active.end()) {
            cout << "switched queues" << endl;
        }
    }
};

}

#endif
