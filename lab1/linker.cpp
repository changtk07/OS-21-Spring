#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

using namespace std;

class Linker {
private:
    enum PARSE_ERROR {
        NUM_EXPECTED,
        SYM_EXPECTED,
        ADDR_EXPECTED,
        SYM_TOO_LONG,
        TOO_MANY_DEF_IN_MODULE,
        TOO_MANY_USE_IN_MODULE,
        TOO_MANY_INSTR
    };
    
    class Tokenizer {
    private:
        ifstream ifs_;
        istringstream iss_;
        string token_;
        int linenum_, lineoffset_, finalpos_;
        
        bool loadline() {
            string line;
            if (getline(ifs_, line)) {
                finalpos_ = static_cast<int>(line.size()) + 1;
                iss_.clear();
                iss_.str(line + " ");
                linenum_++;
                return true;
            }
            return false;
        }
        
        bool getToken() {
            while (!(iss_ >> token_)) {
                if (!loadline()) {
                    lineoffset_ = finalpos_;
                    return false;
                }
            }
            lineoffset_ = static_cast<int>(iss_.tellg()) - static_cast<int>(token_.size()) + 1;
            return true;
        }
    public:
        Tokenizer(const string infile) : linenum_(0) {
            ifs_.open(infile);
        }
        
        template<class InputIterator, class UnaryPredicate>
        static bool all_of (InputIterator first, InputIterator last, UnaryPredicate pred) {
            while (first!=last) {
                if (!pred(*first)) return false;
                ++first;
            }
            return true;
        }
        
        static bool isnum(const string& token) {
            return !token.empty()
            && all_of(token.begin(), token.end(), ::isdigit);
        }
        
        // only test [a-Z][a-Z0-9]*, doesn't test length
        static bool issymbol(const string& token) {
            return !token.empty()
            && isalpha(token[0])
            && all_of(token.begin()+1, token.end(), ::isalnum);
        }
        
        static bool isIAER(const string& token) {
            return token=="I" || token=="A" || token=="E" || token=="R";
        }
        
        bool eof() {
            auto offset = iss_.tellg();
            if (iss_ >> token_) {
                iss_.seekg(offset);
                return false;
            }
            offset = ifs_.tellg();
            if (ifs_ >> token_) {
                ifs_.seekg(offset);
                return false;
            }
            return true;
        }
        
        int read_int() {
            if (!getToken() || !isnum(token_)) {
                throw PARSE_ERROR::NUM_EXPECTED;
            }
            return stoi(token_);
        }
        
        string read_symbol() {
            if (!getToken() || !issymbol(token_)) {
                throw PARSE_ERROR::SYM_EXPECTED;
            }
            if (token_.size() > 16) {
                throw PARSE_ERROR::SYM_TOO_LONG;
            }
            return token_;
        }
        
        string read_IAER() {
            if (!getToken() || !isIAER(token_)) {
                throw PARSE_ERROR::ADDR_EXPECTED;
            }
            return token_;
        }
        
        void parseerror(int errcode) {
            vector<string> errstr {
                "NUM_EXPECTED",
                "SYM_EXPECTED",
                "ADDR_EXPECTED",
                "SYM_TOO_LONG",
                "TOO_MANY_DEF_IN_MODULE",
                "TOO_MANY_USE_IN_MODULE",
                "TOO_MANY_INSTR"
            };
            cout << "Parse Error line " << linenum_ << " offset " << lineoffset_ << ": " << errstr[errcode] << endl;
        }
    };
    
    const string infile_;
    unordered_map<string, int> symbol_table_;
    vector<int> memory_map_;
    
    void print_symbol_table(vector<string>& deflist, unordered_map<string, bool> md) {
        cout << "Symbol Table" << endl;
        for (string& symbol : deflist) {
            cout << symbol << "=" << symbol_table_[symbol];
            if (md[symbol]) {
                cout << " Error: This variable is multiple times defined; first value used";
            }
            cout << endl;
        }
        cout << endl;
    }
    
    void print_memory_map(vector<string>& instrerr, vector<pair<int, string>>& moderr) {
        cout << "Memory Map" << endl;
        int p = 0;
        for (int i = 0; i < memory_map_.size(); i++) {
            while (p < moderr.size() && i == moderr[p].first) {
                cout << moderr[p].second;
                p++;
            }
            cout << setw(3) << setfill('0') << i;
            cout << ": " << setw(4) << setfill('0') << memory_map_[i];
            cout << instrerr[i] << endl;
        }
        while (p < moderr.size()) {
            cout << moderr[p].second;
            p++;
        }
        cout << endl;
    }
    
public:
    static const int MACHINE_SIZE = 512;
    static const int LIST_SIZE = 16;
    
    Linker(const string infile) : infile_(infile) {}
    
    bool pass1() {
        Tokenizer tokenizer(infile_);
        vector<string> deflist;
        vector<int> defaddr;
        unordered_map<string, bool> mutiple_defined;
        
        int module = 1, module_addr = 0, p = 0;
        
        try {
            while (!tokenizer.eof()) {
                // parse define list
                int defcount = tokenizer.read_int();
                if (defcount > LIST_SIZE) {
                    throw PARSE_ERROR::TOO_MANY_DEF_IN_MODULE;
                }
                for (int i = 0; i < defcount; i++) {
                    string symbol = tokenizer.read_symbol();
                    int rel_addr = tokenizer.read_int();
                    deflist.push_back(symbol);
                    defaddr.push_back(rel_addr);
                }
                
                // parse use list
                int usecount = tokenizer.read_int();
                if (usecount > LIST_SIZE) {
                    throw PARSE_ERROR::TOO_MANY_USE_IN_MODULE;
                }
                while (usecount-- > 0) {
                    tokenizer.read_symbol();
                }
                
                // parse program text
                int codecount = tokenizer.read_int();
                if (module_addr+codecount > MACHINE_SIZE) {
                    throw PARSE_ERROR::TOO_MANY_INSTR;
                }
                for (int i = 0; i < codecount; i++) {
                    tokenizer.read_IAER();
                    tokenizer.read_int();
                }
                
                // Generate symbol table
                while (p < deflist.size()) {
                    const string symbol = deflist[p];
                    
                    if (symbol_table_.find(symbol) == symbol_table_.end()) {
                        symbol_table_[symbol] = defaddr[p] + module_addr;
                        mutiple_defined[symbol] = false;
                    } else {
                        mutiple_defined[symbol] = true;
                        deflist.erase(deflist.begin()+p);
                        defaddr.erase(defaddr.begin()+p);
                        p--;
                    }
                    
                    // check symbol relative address size
                    const int rel_addr = symbol_table_[symbol] - module_addr;
                    if (rel_addr >= codecount) {
                        cout << "Warning: Module " << module << ": " << symbol << " too big " << rel_addr << " (max=" << codecount-1 << ") assume zero relative" << endl;
                        symbol_table_[symbol] = module_addr;
                    }
                    
                    p++;
                }
                
                module++;
                module_addr += codecount;
            }
            print_symbol_table(deflist, mutiple_defined);
        } catch (PARSE_ERROR errcode) {
            tokenizer.parseerror(errcode);
            return false;
        }
        return true;
    }
    
    void pass2() {
        Tokenizer tokenizer(infile_);
        vector<vector<string>> deforder;
        unordered_map<string, bool> definelist_usage;
        unordered_set<string> unique_defsymbol;
        vector<string> instrerr;
        vector<pair<int, string>> moderr;
        
        int module_addr = 0;
        
        while (!tokenizer.eof()) {
            //parse define list
            int defcount = tokenizer.read_int();
            vector<string> deflist;
            for (int i = 0; i < defcount; i++) {
                string symbol = tokenizer.read_symbol();
                tokenizer.read_int();
                if (unique_defsymbol.find(symbol) == unique_defsymbol.end()) {
                    deflist.push_back(symbol);
                }
                definelist_usage[symbol]; // insert default value false, if non-existance
                unique_defsymbol.insert(symbol);
            }
            deforder.push_back(deflist);
            
            // parse use list
            vector<string> uselist;
            vector<bool> uselist_usage;
            int usecount = tokenizer.read_int();
            while (usecount-- > 0) {
                string symbol = tokenizer.read_symbol();
                uselist.push_back(symbol);
                uselist_usage.push_back(false);
            }
            
            // parse program text
            int codecount = tokenizer.read_int();
            for (int i = 0; i < codecount; i++) {
                string type = tokenizer.read_IAER();
                string err = "";
                int instr = tokenizer.read_int();
                int opcode = instr / 1000;
                int operand = instr % 1000;
                if (type == "I") {
                    if (instr >= 10000) {
                        opcode = 9;
                        operand = 999;
                        err = " Error: Illegal immediate value; treated as 9999";
                    }
                } else if (opcode >= 10) {
                    opcode = 9;
                    operand = 999;
                    err = " Error: Illegal opcode; treated as 9999";
                } else if (type == "R") {
                    if (operand >= codecount) {
                        operand = 0;
                        err = " Error: Relative address exceeds module size; zero used";
                    }
                    operand += module_addr;
                } else if (type == "E") {
                    if (operand >= uselist.size()) {
                        err = " Error: External address exceeds length of uselist; treated as immediate";
                    } else if (symbol_table_.find( uselist[operand] ) == symbol_table_.end()) {
                        uselist_usage[operand] = true;
                        err = " Error: " + uselist[operand] + " is not defined; zero used";
                        operand = 0;
                    } else {
                        uselist_usage[operand] = true;
                        operand = symbol_table_[ uselist[operand] ];
                    }
                } else {
                    if (operand >= MACHINE_SIZE) {
                        operand = 0;
                        err = " Error: Absolute address exceeds machine size; zero used";
                    }
                }
                memory_map_.push_back(opcode*1000 + operand);
                instrerr.push_back(err);
            }
            
            // Appeared in uselist but not actually used
            moderr.push_back(make_pair(module_addr+codecount, ""));
            for (int i = 0; i < uselist.size(); i++) {
                definelist_usage[ uselist[i] ] |= uselist_usage[i];
                if (!uselist_usage[i]) {
                    string err = "Warning: Module " + to_string(deforder.size()) + ": " + uselist[i] + " appeared in the uselist but was not actually used\n";
                    moderr.back().second += err;
                }
            }
            module_addr += codecount;
        }
        
        print_memory_map(instrerr, moderr);
        
        // Defined but never used Warning
        for (int i = 0; i < deforder.size(); i++) {
            for (const string& symbol : deforder[i]) {
                if (!definelist_usage[symbol]) {
                    cout << "Warning: Module " << i+1 << ": "
                         << symbol << " was defined but never used" << endl;
                }
            }
        }
    }
};

int main(int argc, const char * argv[]) {
    Linker linker(argv[1]);

    if (linker.pass1()) {
        linker.pass2();
    }
    
    return 0;
}
