#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <list>
#include <vector>
#include <algorithm>
#include <unistd.h>

using namespace std;

bool OPTION_V = false;
bool OPTION_F = false;
bool OPTION_Q = false;

unsigned long current_time = 0;
unsigned long current_track = 0;
unsigned long total_time = 0;
unsigned long tot_movement = 0;
bool direction = true;  // true for up, false for down

struct Request {
    unsigned long id;
    unsigned long arrive_time;
    unsigned long target_track;
    unsigned long start_time;
    unsigned long end_time;
    
    Request(unsigned long id,
            unsigned long timestamp,
            unsigned long track) :
        id(id),
        arrive_time(timestamp),
        target_track(track),
        start_time(0),
        end_time(0) {}
};

ostream& operator << (ostream& os, const Request* r) {
    os << setw(5) << r->arrive_time << " "
       << setw(5) << r->start_time << " "
       << setw(5) << r->end_time << endl;
    return os;
}

vector<Request*> requests;
Request* active = nullptr;

class Scheduler {
protected:
    list<Request*> activeq;
    
public:
    virtual ~Scheduler() {}
    virtual Request* get_next_io() = 0;
    virtual void add_request(Request* r) { activeq.push_back(r); }
    virtual bool empty() { return activeq.empty(); }
};

class FifoScheduler : public Scheduler {
public:
    Request* get_next_io() {
        Request* r = activeq.front();
        activeq.pop_front();
        return r;
    }
};

class SstfScheduler : public Scheduler {
public:
    Request* get_next_io() {
        if (OPTION_Q) cout << "\t";
        auto sh = activeq.begin();
        for (auto it = activeq.begin(); it != activeq.end(); it++) {
            unsigned long curr_dist =
                (*it)->target_track > ::current_track ?
                (*it)->target_track - ::current_track :
                ::current_track - (*it)->target_track;
            unsigned long shortest_dist =
                (*sh)->target_track > ::current_track ?
                (*sh)->target_track - ::current_track :
                ::current_track - (*sh)->target_track;
            if (curr_dist < shortest_dist) {
                sh = it;
            }
            if (OPTION_Q) {
                cout << (*it)->id << ":" << curr_dist << " ";
            }
        }
        if (OPTION_Q) cout << endl;
        Request* r = *sh;
        activeq.erase(sh);
        return r;
    }
};

class LookScheduler : public Scheduler {
protected:
    bool ptarget = false;
    bool pdist = true;
public:
    Request* get_next_io() {
        list<Request*> hilist, lolist;
        auto hi = activeq.end(), lo = activeq.end();
        for (auto it = activeq.begin(); it != activeq.end(); it++) {
            if ((*it)->target_track >= ::current_track) {
                if (hi == activeq.end() ||
                    (*it)->target_track < (*hi)->target_track) {
                    hi = it;
                }
                hilist.emplace_back((*it));
            }
            if ((*it)->target_track <= ::current_track) {
                if (lo == activeq.end() ||
                    (*it)->target_track > (*lo)->target_track) {
                    lo = it;
                }
                lolist.emplace_back((*it));
            }
        }
        auto& next = ::direction ? hi : lo;
        bool change = next == activeq.end();
        if (change) next = ::direction ? lo : hi;
        Request* r = *next;
        activeq.erase(next);
        if (OPTION_Q) {
            auto& list = ::direction ? hilist : lolist;
            if (change) {
                list = ::direction ? lolist : hilist;
                cout << "\tGet: () --> change direction to "
                     << (::direction ? -1 : 1) << endl;
            }
            cout << "\tGet: (";
            printq(list);
            cout << ") --> " << r->id
                 << " dir=" << (::direction^change ? 1 : -1) << endl;
        }
        return r;
    }
    
    void printq(list<Request*>& queue, bool psign=true) {
        for (const auto& r : queue) {
            cout << r->id;
            if (ptarget) cout << ":" << r->target_track;
            if (pdist) {
                bool sign = r->target_track >= ::current_track;
                unsigned long dist = sign ? r->target_track-::current_track
                                          : ::current_track-r->target_track;
                cout << ":" << (sign||psign ? "" : "-") << dist;
            }
            cout << " ";
        }
    }
};

class CLookScheduler : public LookScheduler {
public:
    Request* get_next_io() {
        list<Request*> list;
        auto next = activeq.end(), lo = activeq.begin();
        for (auto it = activeq.begin(); it != activeq.end(); it++) {
            if ((*it)->target_track >= ::current_track) {
                if (next == activeq.end() ||
                    (*it)->target_track < (*next)->target_track) {
                    next = it;
                }
                list.emplace_back(*it);
            }
            if ((*it)->target_track < (*lo)->target_track) {
                lo = it;
            }
        }
        bool change = next == activeq.end();
        if (change) next = lo;
        Request* r = *next;
        activeq.erase(next);
        if (OPTION_Q) {
            if (change) {
                cout << "\tGet: () --> go to bottom and pick "
                     << r->id << endl;
            }
            else {
                cout << "\tGet: (";
                printq(list);
                cout << ") --> " << r->id << endl;
            }
        }
        return r;
    }
};

class FLookScheduler : public LookScheduler {
private:
    list<Request*> addq;
    bool swap = false;
public:
    FLookScheduler() { ptarget=true; }
    
    void add_request(Request* r) {
        (::active ? addq : activeq).push_back(r);
        if (OPTION_Q) {
            pdist = false;
            cout << "   Q=" << (::active ? !swap : swap) << " ( ";
            printq(::active ? addq : activeq);
            cout << ")" << endl;
        }
    }
    
    Request* get_next_io() {
        if (activeq.empty()) {
            activeq.swap(addq);
            swap = !swap;
        }
        if (OPTION_Q) {
            pdist = true;
            cout << "AQ=" << swap << " dir=" << (::direction ? 1 : -1)
                 << " curtrack=" << ::current_track << ":  Q[0] = ( ";
            printq(swap ? addq : activeq, false);
            cout << ")  Q[1] = ( ";
            printq(swap ? activeq : addq, false);
            cout << ") " << endl;
        }
        Request* r = LookScheduler::get_next_io();
        if (OPTION_F) {
            cout << ::current_time << ": "
                 << setw(7) << r->id << " get Q=" << swap << endl;
        }
        return r;
    }
    
    bool empty() {
        return addq.empty() && activeq.empty();
    }
};

Scheduler* sched;

void load_requests(string filename) {
    ifstream infile(filename);
    istringstream buffer;
    string line;
    while (getline(infile, line)) {
        if (line[0] == '#') continue;
        buffer.clear();
        buffer.str(line);
        
        unsigned long timestamp, track;
        buffer >> timestamp >> track;
        Request *r = new Request(requests.size(), timestamp, track);
        
        requests.push_back(r);
    }
}

void simulation() {
    if (OPTION_V) {
        cout << "TRACE" << endl;
    }
    
    auto next_req = requests.begin();
    while (true) {
        // check request queue and if arrive at current time then add to ioqueue
        // no two IO requests arrive at the same time
        if (next_req != requests.end() &&
            (*next_req)->arrive_time == ::current_time) {
            if (OPTION_V) {
                cout << ::current_time << ": "
                     << setw(5) << (*next_req)->id << " add "
                     << (*next_req)->target_track << endl;
            }
            ::sched->add_request( *next_req );
            next_req++;
        }
        
        if (::active) {
            // check if active IO is complete
            if (::active->target_track == ::current_track) {
                ::active->end_time = ::current_time;
                if (OPTION_V) {
                    cout << ::current_time << ": "
                         << setw(5) << ::active->id << " finish "
                         << (::current_time - ::active->arrive_time) << endl;
                }
                ::active = nullptr;
                continue;
            }
            else {
                ::current_track += ::direction ? 1 : -1;
                ::tot_movement++;
            }
        }
        else if (!::sched->empty()) {
            // if no active IO and there are pending IO, start new IO
            ::active = ::sched->get_next_io();
            ::active->start_time = ::current_time;
            if (::current_track != ::active->target_track) {
                ::direction = ::current_track < ::active->target_track;
            }
            if (OPTION_V) {
                cout << ::current_time << ": "
                     << setw(5) << ::active->id << " issue "
                     << ::active->target_track << " "
                     << ::current_track << endl;
            }
            continue;
        }
        
        // if no IO request is active now, no IO requests pending and no active
        // exit simulation
        if (!::active && next_req==requests.end() && ::sched->empty()) {
            ::total_time = ::current_time;
            break;
        }
        
        ::current_time++;
    }
}

void print_info() {
    unsigned long tot_turnaround = 0;
    unsigned long tot_waittime = 0;
    unsigned long max_waittime = 0;
    for (size_t i = 0; i < requests.size(); i++) {
        Request* req = requests[i];
        tot_turnaround += req->end_time - req->arrive_time;
        tot_waittime += req->start_time - req->arrive_time;
        max_waittime = max( max_waittime, req->start_time - req->arrive_time );
        cout << setw(5) << i << ": " << req;
        delete req;
    }
    
    double avg_turnaround = (double)tot_turnaround / requests.size();
    double avg_waittime = (double)tot_waittime / requests.size();
    cout << "SUM: " << ::total_time << " "
         << ::tot_movement << " "
         << fixed << setprecision(2) << avg_turnaround << " "
         << fixed << setprecision(2) << avg_waittime << " "
         << max_waittime << endl;
}

bool options(int& argc, char* const argv[], char& algo) {
    opterr = 0;
    int o;
    while ((o = getopt(argc, argv, "vfqs:")) != -1) {
        switch (o) {
            case 's':
                algo = optarg[0];
                break;
            case 'v':
                ::OPTION_V = true;
                break;
            case 'f':
                ::OPTION_F = true;
                break;
            case 'q':
                ::OPTION_Q = true;
                break;
            case '?':
                if (optopt == 's')
                    cerr << "Option -" << char(optopt)
                         << "requires an argument." << endl;
                else if (isprint (optopt))
                    cerr << "Unknown option -" << char(optopt)
                         << "." << endl;
                else
                    cerr << "Unknown option character \\x"
                         << hex << optopt << "." << endl;
            default:
                return false;
        }
    }
    return true;
}

int main(int argc, char* const argv[]) {
    char algo = 'i';
    if(!options(argc, argv, algo)) {
        return 1;
    }
    switch (algo) {
        case 'i':
            sched = new FifoScheduler;
            break;
        case 'j':
            sched = new SstfScheduler;
            break;
        case 's':
            sched = new LookScheduler;
            break;
        case 'c':
            sched = new CLookScheduler;
            break;
        case 'f':
            sched = new FLookScheduler;
            break;
        default:
            cerr << "Unknown scheduler alogrithm: " << optarg[0]
                 << "'." << endl;
    }
    
    argc -= optind;
    argv += optind;
    if (argc < 1) {
        cerr << "Input file is required." << endl;
        return 1;
    }
    
    load_requests(argv[0]);
    
    simulation();
    delete sched;
    
    print_info();
    
    return 0;
}
