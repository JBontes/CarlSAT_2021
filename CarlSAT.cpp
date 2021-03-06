/*******************************************************************
Copyright 2020 Johan Bontes (johan at digitsolutions dot nl)
and Amazon.com, Inc and its affiliates

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*********************************************************************/

#include <iomanip>
#include "CarlSAT.hpp"
#include "FilePreprocess.hpp"
#include <x86intrin.h>
//#include <mcheck.h>

//do not call this unless you're sure there is a next bit.
template <bool satlit>
int NextSetBit(const bitset<64>* bits, int previous = -1) {
    //walking through the satisfied bits of a clause using tzcnt is much faster than testing each single bit
    //esp for clauses that have many unsatisfied literals.
    int i;
    uint32_t next;
    //get followup bits
    previous++;
    i = (previous / 64); //starting chunk
    const auto firstmask = uint64_t(-1) << (previous % 64); //perhaps we can remove the % 64 part due to x86 mechanics
    uint64_t data;
    if constexpr (satlit) { data =  bits[i].to_ullong(); }
    else {                  data = ~bits[i].to_ullong(); }
    data &= firstmask;
    next = _tzcnt_u64(data);
    while (64 == next) {
        uint64_t data;
        if constexpr (satlit) { data =  bits[++i].to_ullong(); }
        else                  { data = ~bits[++i].to_ullong(); }
        next = _tzcnt_u64(data);
    }
    return (next + (i * 64));
}

template <bool satlit>
int PureCMS_t::Get_Nth_sat_lit(const uint32_t cls, const uint32_t n) const {
    assert(cls < hClauseCount);
    if constexpr(satlit) { assert(n < h_SatisfiedLitCount[cls]); }
    else { assert( n < (h_LiteralsInClauseCount[cls] - h_SatisfiedLitCount[cls]) ); }


    //first see in which chunk the literal falls
    auto chunk = 0;
    auto i = n;
    uint64_t lastchunk;
    auto lastcount = 0;
    while (i > 0) {
        lastchunk = h_SATLiteralBits[cls][chunk++].to_ullong();
        lastcount = _popcnt64(lastchunk);
        if constexpr (!satlit) { lastcount = 64 - lastcount; } //the number of falsified lits instead
        i -= lastcount;
    }
    if (0 == i) { //it's the first set bit in the current chunk
        if constexpr(satlit) { return _tzcnt_u64( h_SATLiteralBits[cls][chunk].to_ullong()); }
        else {                 return _tzcnt_u64(~h_SATLiteralBits[cls][chunk].to_ullong()); }
        //    -----------------------------------^
    } else { //i < 0, rewind one chunk
        //https://stackoverflow.com/a/27453505/650492
        if constexpr (satlit) { return _tzcnt_u64(_pdep_u64(1ULL << (lastcount - i),  lastchunk )); }
        else {                  return _tzcnt_u64(_pdep_u64(1ULL << (lastcount - i), ~lastchunk )); }
        //                                        -----------------------------------^
    }
}

enum CardKind_t { ck_None, ck_AtLeast, ck_AtMost }; //for now ignore equals, great, lessthan

void PureCMS_t::allocate() {
    assert(nVars > 0);
    assert(nClauses > 0);
    int var_mem = nVars + 2; //dummy variable 0 and an extra sentinal
    int cls_mem = nClauses + 1;
    h_K_atLeast = new uint32_t[cls_mem];                   //cardinality at least
    h_SATLiteralBits = new bitset<64>*[cls_mem];        //bitset with sat bits in card clauses

    h_LiteralsInClauseCount = new uint32_t[cls_mem]();
    h_LiteralsInClauses = new int* [cls_mem];
    h_SatisfiedLitCount = new uint32_t[cls_mem]();
    h_ClauseWeights = new uint64_t[cls_mem];
    fill_n(h_ClauseWeights, cls_mem, 1);

    s_CriticalVarInSoftClause = new int[cls_mem](); //over allocation. but that's OK
    //vweight = new int[var_mem]();
    v_lits_hard_clause_id = new Occurrence_t* [var_mem];
    v_lits_soft_clause_id = new int* [var_mem];
    //v_lits_pos_hard = new uint32_t* [var_mem];
    //v_lits_hard_neighbor = new int* [var_mem];
    v_lits_hard_size = new int[var_mem]();
    v_lits_soft_size = new int[var_mem]();
    v_lits_hard_kcount = new int[var_mem]();
    soln = Atom_t::NewHostAtom(var_mem, parameters.Strategy);
    best_soln = Atom_t::NewHostAtom(var_mem, parameters.Strategy);
    time_stamp = new int64_t[var_mem]();
    //dscore = new int[var_mem]();
    s_MakeScore = new int64_t[var_mem]();
    s_BreakScore = new int64_t[var_mem]();
    s_PlainBreakScore = new int64_t[var_mem](); //used to see if we'll exceeded the allowed cost in phase II

    h_MakeScore = new int64_t[var_mem]();
    h_BreakScore = new int64_t[var_mem]();
    degree = new int[var_mem]();

    //valid_score = new int[var_mem];
    //fill_n(valid_score, var_mem, INT_MAX);
    // = new int[var_mem]();
    //idx_in_unsat_hard = new int[var_mem];
    //fill_n(idx_in_unsat_hard, var_mem, -1);

    //unsat_in_soft = new int[var_mem]();
    //idx_in_unsat_soft = new int[var_mem];
    //fill_n(idx_in_unsat_soft, var_mem, -1);

    tabu_remove = new int[var_mem];
    tabu_list = new int[var_mem];
    //vars_sorted_by_score = new int[var_mem]; //do we need this?, just keeping track of the best var would also get this.

    //the soft clause data is located in the top of the hard clause lists.
    //we then set the pointer at the correct location, so that it works out.
}

void PureCMS_t::freeMemory() {
    return;
    delete Cache;
    delete[] h_LiteralsInClauseCount;
    for (uint32_t i = 0; i < nClauses; ++i) { delete[] h_LiteralsInClauses[i]; }
    delete[] v_lits_hard_size;
    delete[] v_lits_soft_size;
    for (uint32_t i = 1; i <= nVars; ++i) {
        delete[] v_lits_hard_clause_id[i];
        delete[] v_lits_soft_clause_id[i];
        //delete[] v_lits_pos_hard[i];
        //delete[] v_lits_hard_neighbor[i];
    }
    delete[] v_lits_hard_clause_id;
    delete[] v_lits_soft_clause_id;
    //delete[] v_lits_pos_hard;
    //delete[] v_lits_hard_neighbor;
    delete[] h_LiteralsInClauses;
    delete[] h_SatisfiedLitCount;
    delete[] s_CriticalVarInSoftClause;
    delete[] h_ClauseWeights;
    delete[] h_SATLiteralBits;
    delete[] h_K_atLeast;
    //delete[] vweight;
    delete[] time_stamp;

    delete[] h_MakeScore;
    delete[] h_BreakScore;
    delete[] s_MakeScore;
    delete[] s_BreakScore;

    //delete[] unsat_in_hard;
    //delete[] idx_in_unsat_hard;
    //delete[] unsat_in_soft;
    //delete[] idx_in_unsat_soft;
    delete[] tabu_remove;
    delete[] tabu_list;
    //delete[] vars_sorted_by_score;
}

void PureCMS_t::initParams() {
    hClauseCount = 0;
    sClauseCount = 0;
    best_cost = INT64_MAX;
    cost = 0;
    flipcount = 0;
    NextFalseIndex = -1;
    step = 1;
    StartTime = chrono::high_resolution_clock::now();
    cutoff_time = 500;
    avg_weight = 1;
    delta_total_weight = 0;
    p_scale = 0.3;
    threshold = (int)(0.5 * nVars);
    total_hard_lits = 0;
    total_soft_lits = 0;
    s_FalseCount = h_FalseCount = tabu_list_size = 0;
    unit_fixed_weight = 0;
    best_time = 0.0;
    //v_lits_soft_unsat_count = 0;
    //v_lits_hard_unsat_count = 0;
    bound_mod = parameters.LoopLengthPhase12;
    try_num = 50; //BMS in reset

    try_pick = 1;
}

void PureCMS_t::update_best_soln() {
    if (cost < best_cost) {
        best_cost = cost;
        soln->cost = cost;
        soln->s_FalseCount = s_FalseCount;
        soln->time = Millisecs(StartTime, now());
        soln->hasImproved = true;
        soln->CloneInto(best_soln);

        best_time = getRuntimeInMs();
        if(parameters.ExtraChecks && (!verify()) ) { cout << "c wrong" << endl; exit(0); }
    }
}

template <int LogLevel>
void PureCMS_t::showSoln(const string& filename) const {
    verify<LogLevel>(); //Never save a witness to disk without verifying it. Do NOT remove
    ofstream fout(filename);
    if (!fout) {
        cout << "c something wrong with output_file." << endl;
        Log(0, "Cannot open output file \"%s\" for writing, witness will not be written\n", filename.c_str());
        return;
    }
    fout << "c problem instance " << std::filesystem::path(parameters.Filename).filename() << endl;
    fout << "c ClauseCount: " << nClauses << " VarCount: " << nVars << endl;
    fout << "c # of SoftClauses: " << sClauseCount << " # SAT: " << SATSoftCount << " # UNSAT " << sClauseCount - SATSoftCount << endl;
    assert(sClauseCount - SATSoftCount >= 0);
    fout << "c # of HardClauses: " << hClauseCount << " all of which are SAT" << endl;
    fout << "c Solve took " << best_time << "ms" << endl;
    const auto time = std::time(nullptr);
    fout << "c date YMD-HMS" << std::put_time(std::localtime(&time), " %Y-%m-%d  %H:%M:%S ") << endl;
    fout << "s UNKNOWN \n";
    fout << "o "<< best_cost + unit_fixed_weight << endl;
    fout << "v ";
    for (decltype(nVars) i = 1; i <= nVars; ++i) {
        if (  (!best_soln->State(i) && lit_in_hClause_is_positive)
            || (best_soln->State(i) && !lit_in_hClause_is_positive))  { fout << "-"; }
        fout << i << " ";
    }
    Log(1, "True literals: %i, False literals: %i", best_soln->PopCount(), nVars - best_soln->PopCount());
    fout << endl;
    fout.close();
    Log(1, "c Time: %.0f ms", best_time);
}

template <int LogLevel>
void PureCMS_t::showSoln(const bool needVarify, const bool showassignment) const {
    if (needVarify && !verify()) {
        cout << "c The soln is wrong." << endl;
        return;
    }
    cout << "s UNKNOWN\n"; 
    cout << "o " << best_cost + unit_fixed_weight << endl;
    if (showassignment) {
        cout << "v ";
        for (decltype(nVars) i = 1; i <= nVars; ++i) {
            if (   (!best_soln->State(i) &&  lit_in_hClause_is_positive)
                ||  (best_soln->State(i) && !lit_in_hClause_is_positive))  { cout << "-"; }
            cout << i << " ";
        }
    }
    Log(1, "True literals: %i, False literals: %i", best_soln->PopCount(), nVars - best_soln->PopCount());
    Log(1, "Intermediate: Score = %llu, True literals: %i, False literals: %i", soln->cost, soln->PopCount(), nVars - soln->PopCount());
    Log(1, "c Time: %.3f s", best_time / 1'000'000);
}

char* readCardKind(char* pos, CardKind_t& CardKind) {
    auto oldpos = pos;
    CardKind = ck_None;
    while (' ' == *pos) { ++pos; }
    switch (*pos) {
        case '>': CardKind = ck_AtLeast; //fallthrough
        case '<': if (ck_None == CardKind) { CardKind = ck_AtMost; }
            pos++;
            if ('=' != *pos) { cout << "error, not a valid wcard file" << endl; exit(-1); }
            return ++pos;
        default: return oldpos;
    }
}

template <typename T>
char* readInt(char* pos, T& i) {
    static_assert(is_integral<T>::value && (sizeof(T) == sizeof(int) || sizeof(T) == sizeof(int64_t)) );
    i = 0;
    auto sym = true;
    while (*pos < '0' || *pos > '9') {
        if constexpr (is_unsigned<T>::value) { assert('-' != *pos ); }
        else { if ('-' == *pos) { sym = false; } }
        ++pos;
    }
    while (*pos >= '0' && *pos <= '9') {
        i = (i * 10) + (*pos - '0');
        ++pos;
    }
    if constexpr (is_signed<T>::value) {
        if (!sym) { i = -i; }
    }
    return pos;
}

template <typename T>
bool sign(const T& value) {
    return (value < 0);
}

std::string nexttoken(char const * text) {
    locale loc;
    auto start = 0;
    while ( ((text[start] <= ' ') || (text[start] >= 127))) { start++; }
    auto end = start + 1;
    while (!((text[end]   <= ' ') || (text[end]   >= 127))) { end++; }
    const auto size = end - start;
    std::string result; result.resize(size);
    for (auto i = 0; i < size; i++) {
        result[i] = tolower(text[start++], loc);
    }
    return result;
}

void PureCMS_t::InitClauseLengthCorrections() {    
    s_ClauseLengthCorrections = new int[s_LongestClause + 1];
    //todo: make this selectable
    for (decltype(s_LongestClause) i = 1; i <= s_LongestClause; i++) {
        auto LengthCorr = s_LongestClause - (i-1);
        LengthCorr = 1 + log2(LengthCorr);
        s_ClauseLengthCorrections[i] = LengthCorr;
    }
}


bool PureCMS_t::build(const Parameters_t& parameters) {
    this->parameters = parameters;
    auto hasInputFile = false;
    hasResumed = false;
    if (parameters.InputFilename.length() > 0) {
        //see if this file exists
        namespace fs = std::filesystem;
        std::filesystem::path f{ parameters.InputFilename };
        hasInputFile = (std::filesystem::exists(f));
    }
    if (hasInputFile) { 
        if (read_state()) { 
            hasResumed = true;
            return true; 
        }
     }  //<<<<<<<<<<<<<<<<<<  don't read problem file if we are resuming.


    ifstream fin(parameters.Filename);
    if (!fin) {
        cout << "c file " << parameters.Filename << " not found" << endl;
        Parameters_t dummy = parameters;
        ParseOptions(0, nullptr, dummy); //print help
        return false;
    }

    fin.seekg(0, fin.end);
    size_t file_len = fin.tellg();
    fin.seekg(0, fin.beg);
    char* data = new char[file_len + 1];
    fin.read(data, file_len);
    fin.close();
    data[file_len] = '\0';
    char* pos = data;

    //skip comments
    while ('c' == *pos) { while ('\n' != *(pos++)) {;/*do nothing*/} }
    //p wcard #vars #Clauses #HardClauseWeight
    if ('p' != *pos++) { printf("c \"%s\" is not a valid wcard file", parameters.Filename.c_str()); exit(-1); }
    while (' ' == *++pos) {;/*do nothing*/}
    //wncf means normal weighted cnf
    //cnf means plain cnf (all clauses are hard, this solver cannot handle that)
    //wcard means weighted cardinality
    filetype_t filetype;
    const auto formatstr = nexttoken(pos);
    if      ("wcard" == formatstr) { filetype = ft_wcard; }
    else if ("wcnf"  == formatstr) { filetype = ft_wcnf; }
    else { printf("c error, cannot process %s files, only wcard and wcnf files", formatstr.c_str()); exit(0); }
    pos = readInt(pos, nVars);
    pos = readInt(pos, nClauses);
    pos = readInt(pos, hClauseWeight);
    allocate();
    initParams();

    int cur_lit, cur_soft_lit;
    auto last_soft_lit = 0;
    uint64_t sym;
    s_LiteralsInClauses = &h_LiteralsInClauses[nClauses]; //s grows down, index using negative indexes
    s_LiteralsInClauseCount = &h_LiteralsInClauseCount[nClauses];
    s_ClauseWeights = &h_ClauseWeights[nClauses];
    int* a = new int[nVars];
    s_LongestClause = 0;
    for (auto idx = nClauses; idx > 0; --idx) {
        auto CardKind = ck_None;
        pos = readInt(pos, sym);
        uint32_t ClauseLength = 0;

        //------------ Hard clauses ------------------------
        if (sym == hClauseWeight) {
            h_K_atLeast[hClauseCount] = 1; //assume a hardclause is a SAT clause
            pos = readInt(pos, cur_lit);
            while (0 != cur_lit && ck_None == CardKind) {
                a[ClauseLength++] = abs(cur_lit);
                //be on the lookout for '>','<'
                pos = readCardKind(pos, CardKind);
                pos = readInt(pos, cur_lit);
            }
            switch (CardKind) {
                case ck_AtLeast:
                    assert(ft_wcard == filetype);
                    h_K_atLeast[hClauseCount] = cur_lit;
                    break;
                case ck_AtMost:
                    assert(ft_wcard == filetype);
                    h_K_atLeast[hClauseCount] = ClauseLength - cur_lit;
                    break;
                default: /*do nothing*/;
            }

            h_LiteralsInClauses[hClauseCount] = new int[ClauseLength + 1];
            const auto ChunkCount = (ClauseLength + 63) / 64;
            h_SATLiteralBits[hClauseCount] = new bitset<64>[ChunkCount]();
            for (uint32_t i = 0; i < ClauseLength; i++) {
                h_LiteralsInClauses[hClauseCount][i] = a[i]; //don't reverse the polarity, because we're assuming pureCardSAT
            }
            h_LiteralsInClauseCount[hClauseCount] = ClauseLength;
            h_ClauseWeights[hClauseCount++] = 1;
        } //if HardClause

        //------------ Soft clauses ------------------------
        else { //note that currently, soft clauses cannot be cardinality clauses, this is a simplification, not some inherent limitation.
            sClauseCount++;
            do {
                pos = readInt(pos, cur_soft_lit);
                if (0 != cur_soft_lit) {
                    assert(0 == last_soft_lit || sign(last_soft_lit) == sign(cur_soft_lit));
                    last_soft_lit = cur_soft_lit;
                    sum_cweight += sym;
                    a[ClauseLength++] = abs(cur_soft_lit);
                }
            } while (0 != cur_soft_lit);
            s_LiteralsInClauseCount[-sClauseCount] = ClauseLength;
            s_LiteralsInClauses[-sClauseCount] = new int[ClauseLength + 1];
            s_ClauseWeights[-sClauseCount] = sym;
            s_LongestClause = max(s_LongestClause, ClauseLength);
            for (uint32_t i = 0; i < ClauseLength; i++) {
                s_LiteralsInClauses[-sClauseCount][i] = a[i];
            }
        } //if SoftClause
    } //for nClauses
    //correct the soft clause indexes, so that grow up again
    //we now know the number of hard and soft clauses, so we can point the s_unsat list to the correct positions
    InitClauseLengthCorrections();

    s_LiteralsInClauseCount = &s_LiteralsInClauseCount[-sClauseCount];
    s_LiteralsInClauses = &s_LiteralsInClauses[-sClauseCount];
    s_ClauseWeights = &s_ClauseWeights[-sClauseCount];
    s_SatisfiedLitCount = &h_SatisfiedLitCount[hClauseCount];

    assert(s_LiteralsInClauseCount == &h_LiteralsInClauseCount[hClauseCount]);
    assert(s_LiteralsInClauses == &h_LiteralsInClauses[hClauseCount]);
    assert(s_ClauseWeights == &h_ClauseWeights[hClauseCount]);

    lit_in_hClause_is_positive = (last_soft_lit < 0);

    //build vars and neighbor
    {
        //degree is the number of literals that share a clause with a lit.
        //todo: currently we do not take polarity into account, perhaps we should?
        auto d = const_cast<int*>(degree);
        for (uint32_t i = 0; i < hClauseCount; ++i) {
            const auto ClauseLength = h_LiteralsInClauseCount[i];
            for (uint32_t j = 0; j < ClauseLength; ++j) {
                const auto var = h_LiteralsInClauses[i][j];

                d[var] += ClauseLength - 1;
                v_lits_hard_size[var]++;
                v_lits_hard_kcount[var] += h_K_atLeast[i];  //K_atLeast denotes the #of hard clauses merged into one
            }
            total_hard_lits += h_LiteralsInClauseCount[i];
        }

        for (decltype(sClauseCount) i = 0; i < sClauseCount; ++i) {
            const auto ClauseLength = s_LiteralsInClauseCount[i];
            for (uint32_t j = 0; j < ClauseLength; ++j) {
                const auto var = s_LiteralsInClauses[i][j];
                d[var] += ClauseLength - 1;
                v_lits_soft_size[var]++;
            }
            total_soft_lits += ClauseLength;
        }
    }

    for (decltype(nVars) i = 1; i <= nVars; ++i) {
        v_lits_hard_clause_id[i] = new Occurrence_t[v_lits_hard_size[i] + 1];
        v_lits_soft_clause_id[i] = new int[v_lits_soft_size[i] + 1]();
        //v_lits_pos_hard[i] =       new uint32_t[hClauseCount]();
        //v_lits_hard_neighbor[i] =  new int[v_lits_hard_size[i] + 1];
    }


    {
        memset(v_lits_hard_size, 0, sizeof(int) * (nVars + 1));  //reset it so that we can use it as a counter here
        for (uint32_t cls = 0; cls < hClauseCount; ++cls) {
            auto cls_sz = h_LiteralsInClauseCount[cls];
            for (uint32_t j = 0; j < cls_sz; ++j) {
                const auto v = h_LiteralsInClauses[cls][j];
                //v_lits_pos_hard[v][cls] = j;
                v_lits_hard_clause_id[v][v_lits_hard_size[v]++] = Occurrence_t(cls, j);
            }
        }

        memset(v_lits_soft_size, 0, sizeof(int) * (nVars + 1)); //reset it so we can use it as a counter here
        for (auto cls = 0; cls < sClauseCount; ++cls) {
            auto cls_sz = s_LiteralsInClauseCount[cls];
            for (uint32_t j = 0; j < cls_sz; ++j) {
                const auto v = s_LiteralsInClauses[cls][j];
                //v_lits_pos_soft[v][cls] = j; //we don't care about the pos for soft clauses
                v_lits_soft_clause_id[v][v_lits_soft_size[v]++] = cls;
            }
        }
    }
    delete[] a;
    delete[] data;
    return true;
}

void PureCMS_t::tabu_add(const int var) {
    tabu_list[tabu_list_size++] = var;
    tabu_remove[var] = 1;
}

//reset all tabu vars
void PureCMS_t::clear_tabu() {
    //memset(tabu_remove, 0, sizeof(int) * (nVars + 1));
    for (auto i = 0; i < tabu_list_size; ++i) {
        tabu_remove[tabu_list[i]] = 0;
    }
    tabu_list_size = 0;
}

void PureCMS_t::sat_soft(const int var) {
    for (auto i = 0; i < v_lits_soft_size[var]; i++) {
        const auto cls = v_lits_soft_clause_id[var][i];
        const auto SatCount = ++s_SatisfiedLitCount[cls];
        if (1 == SatCount) { //from 0 -> 1: clause went from unsat to critical
            assert(0 == s_CriticalVarInSoftClause[cls]);
            cost -= s_ClauseWeights[cls]; //cost does not take clause length into account.
            //1. The clause is now critical, set the sscore of the critical var
            s_FalseCount--;
            ChangeSoftBreak(var, SoftClauseWeight(cls), s_ClauseWeights[cls]);
            //2. The clause is no longer unsat, zero out all the gains to be made by flippings vars
            for (uint32_t idx = 0; idx < s_LiteralsInClauseCount[cls]; idx++) {
                const auto VarInClause = s_LiteralsInClauses[cls][idx];
                assert(VarInClause > 0);
                ChangeSoftMake(VarInClause, -SoftClauseWeight(cls));
                assert(s_MakeScore[VarInClause] >= 0);
            }
        }
        else if (2 == SatCount) { //from 1 -> 2: the clause is no longer critical
            const auto critvar = s_CriticalVarInSoftClause[cls];
            ChangeSoftBreak(critvar, -SoftClauseWeight(cls), -s_ClauseWeights[cls]);
            assert(s_BreakScore[critvar] >= 0);
        }
        s_CriticalVarInSoftClause[cls] ^= var; //update the critical var to the situation *after* the flip
    }
}

void PureCMS_t::unsat_soft(const int var) {
    const auto ClauseCount = v_lits_soft_size[var];
    for (auto i = 0; i < ClauseCount; i++) {
        const auto cls = v_lits_soft_clause_id[var][i];
        const auto SatCount = --s_SatisfiedLitCount[cls];
        if (0 == SatCount) { //from 1 -> 0: the clause is now falsified
            cost += s_ClauseWeights[cls];
            s_FalseCount++;
            //making any of the vars in this clause will revive this clause, update the sscores
            //1. The clause went from critical to false, remove the critical var from the scoring
            assert(isEqual(var, s_CriticalVarInSoftClause[cls]));
            ChangeSoftBreak(var, -SoftClauseWeight(cls), -s_ClauseWeights[cls]);
            assert(s_BreakScore[var] >= 0);
            //2. flipping any var in this clause will make it, adding the clause cost
            for (uint32_t idx = 0; idx < s_LiteralsInClauseCount[cls]; idx++) {
                const auto makevar = s_LiteralsInClauses[cls][idx];
                ChangeSoftMake(makevar, SoftClauseWeight(cls));
            }
        }
        else if (1 == SatCount) { //from 2 -> 1 clause is made critical
            assert(var != s_CriticalVarInSoftClause[cls]);
            const auto critvar = s_CriticalVarInSoftClause[cls] ^ var; //but not by the critical var.
            ChangeSoftBreak(critvar, SoftClauseWeight(cls), s_ClauseWeights[cls]);
        }
        //else n >= 3 -> n >= 2 else nothing happens, because clause is still over-satisfied.
        s_CriticalVarInSoftClause[cls] ^= var; //update the critical var to the situation *after* the flip
        assert(0 != SatCount || 0 == s_CriticalVarInSoftClause[cls]); //make sure xor scheme ends up at 0 if clause gets falsified
    } //for cls
}

template <bool satlit, typename functor_t>
void PureCMS_t::for_each_sat_lit(const int ClauseIndex, functor_t functor) {
    auto LitIndex = -1;
    uint32_t Count = h_SatisfiedLitCount[ClauseIndex];
    if constexpr (!satlit) { Count = h_LiteralsInClauseCount[ClauseIndex] - Count; }
    for (uint32_t dummy = 0; dummy < Count; dummy++) {
        LitIndex = NextSetBit<satlit>(h_SATLiteralBits[ClauseIndex], LitIndex);
        functor(LitIndex);
    }
}


/*The transition diagram for hard scores looks like this

hweight = 1
k_atleast = 3
clauselength = 6
                                                     new                        new
                    hard score                  delta more sat              delta less sat
                 a   b   c   d   e  f        a   b   c   d   e   f       a   b   c   d   e   f    k_oversat    why the score
                 --------------------       ??????????????????????????????????????????????????????????????????      ??????????????????????????????????????????????????????????????????      new        ------------------
abcdef=0         3   3   3   3   3  3        -   -   -   -   -   -      +3  +1  +1  +1  +1  +1      -3         the more undersat the more priority
a=1 bcdef=0      0   2   2   2   2  2       -3  -1  -1  -1  -1  -1       0  +2  +1  +1  +1  +1      -2         undersat clauses must get prioritized
ab=1 cdef=0      0   0   1   1   1  1        0  -2  -1  -1  -1  -1      +1  +1  +2  +1  +1  +1      -1            "                          "
abc=1 def=0     -1  -1  -1   0   0  0       -1  -1  -2  -1  -1  -1      -1  -1  -1   0   0   0       0         critical vars must not be selected
abcd=1 ef=0      0   0   0   0   0  0       +1  +1  +1   0   0   0       0   0   0   0   0   0       1         flipping oversat vars does no harm
abcde=1 f=0      0   0   0   0   0  0        0   0   0   0   0   0       0   0   0   0   0   0       2
abcdef=0         0   0   0   0   0  0        0   0   0   0   0   0       -   -   -   -   -   -       3
*/


//Takes place after the var flip!
template <updatescore_t updatekind>
void PureCMS_t::update_hard_score(const int cls, const int flipvar) {

    const auto SatCount = h_SatisfiedLitCount[cls];
    const int k_criticality = (SatCount - h_K_atLeast[cls]);
    const auto OldClauseWeight = h_ClauseWeights[cls];
    //--------------------------------------------------------------------------------------------------
    // Make
    //--------------------------------------------------------------------------------------------------
    if constexpr (updatekind == us_setvar) { //the clause gets more satisfied
        // if (k_criticality > 1) { //Clause was oversatisfied and is now even more so.
        //     //nothing happens to the score
        // }
        // else
        if (k_criticality == 0) { //Clause was undersat is now critical
            //take each satisfied literal and set its breakscore to clauseweight
            //take each unsat literal and set its makescore to zero
            h_ClauseWeights[cls] = 1; //reset clause weight when that clause becomes satisfied
            for (uint32_t i = 0; i < h_LiteralsInClauseCount[cls]; i++) {
                const auto LitState = HardLiteralBit(cls, i);
                const auto var = h_LiteralsInClauses[cls][i];
                if (flipvar == var) { //flip var was false, is now true
                    ChangeHardBreak(var, OldClauseWeight);
                    ChangeHardMake(var, (-OldClauseWeight));
                    assert(0 == h_MakeScore[var]);
                }
                else {
                    if (LitState) { ChangeHardBreak(var, OldClauseWeight); }
                    else {          ChangeHardMake(var, -OldClauseWeight); }
                }
            }
            h_FalseCount--;
        }
        else if (k_criticality == 1) { //clause was critical, is now oversat
            for (uint32_t i = 0; i < h_LiteralsInClauseCount[cls]; i++) {
                const auto LitState = HardLiteralBit(cls, i);
                const auto var = h_LiteralsInClauses[cls][i];
                if (flipvar == var) { //flip var was false, is now true
                } else {
                    if (LitState) { ChangeHardBreak(var, -OldClauseWeight); }
                }
            }
        }
        else if (k_criticality < 0) { //clause was undersatisfied, is still undersatisfied
            //the remaining unsat vars are slightly less under water
            for_each_sat_lit<false>(cls, [=, this](const int LitIndex){
                const auto CurrentVar = h_LiteralsInClauses[cls][LitIndex];
                ChangeHardMake(CurrentVar, -OldClauseWeight);
                assert(h_MakeScore[CurrentVar] >= (-k_criticality) * OldClauseWeight);
            });
            //The current var is satisfied and thus not weighted.
            ChangeHardMake(flipvar, -OldClauseWeight * ((-k_criticality) + 1));
            assert(h_MakeScore[flipvar] >= 0);
        }
    }
    //--------------------------------------------------------------------------------------------------
    // Break
    //--------------------------------------------------------------------------------------------------
    else if constexpr (updatekind == us_resetvar) { //the clauses gets less satisfied
        // if (k_criticality > 1) { //Clause was oversatisfied and is now less so, nothing happens
        //     /* do nothing */
        // }
        // else
        if (k_criticality == 0) { //1->0 Clause was oversat is now critical
            for_each_sat_lit<true>(cls, [=, this](const int LitIndex) {
                const auto CurrentVar = h_LiteralsInClauses[cls][LitIndex];
                ChangeHardBreak(CurrentVar, OldClauseWeight);
            });
        }
        else if (k_criticality == -1) { //clause was critical, is now undersat
            for (uint32_t i = 0; i < h_LiteralsInClauseCount[cls]; i++) {
                const auto LitState = HardLiteralBit(cls, i);
                const auto var = h_LiteralsInClauses[cls][i];
                if (flipvar == var) { //flip var was true, is now false
                    ChangeHardBreak(flipvar, -OldClauseWeight);
                    assert(h_BreakScore[flipvar] >= 0);
                    ChangeHardMake(flipvar, OldClauseWeight);
                }
                else {
                    if (LitState) { ChangeHardBreak(var, -OldClauseWeight); }
                    else           { ChangeHardMake(var, OldClauseWeight); }
                }
            }
            h_FalseCount++;
        }
        else if (k_criticality < -1) { //clause was undersat, is still more undersatisfied
            const auto UnsatVars = false;
            for_each_sat_lit<UnsatVars>(cls, [=, this](const int LitIndex){
                const auto CurrentVar = h_LiteralsInClauses[cls][LitIndex];
                if (CurrentVar == flipvar) {
                    ChangeHardMake(flipvar, OldClauseWeight * (-k_criticality));
                } else {
                    ChangeHardMake(CurrentVar, OldClauseWeight);
                }
            });
        }
    }
    //--------------------------------------------------------------------------------------------------
    // Recalculate scores
    //--------------------------------------------------------------------------------------------------
    else if constexpr (updatekind == us_recalc) {
        // if (k_criticality >= 1) {} //clause is oversatisfied, score is zero, do nothing
        // else
        if (k_criticality == 0) { //clause is critically satisfied
            for_each_sat_lit<true>(cls, [=, this](const int LitIndex) {
                const auto CurrentVar = h_LiteralsInClauses[cls][LitIndex];
                ChangeHardBreak(CurrentVar, OldClauseWeight);
            });
        }
        else if (k_criticality < 0) { //clause is unsatisfied, weight all unsat literals with undersat count * weight
            for_each_sat_lit<false>(cls, [=, this](const int LitIndex) {
                const auto CurrentVar = h_LiteralsInClauses[cls][LitIndex];
                ChangeHardMake(CurrentVar, OldClauseWeight * (-k_criticality));
            });
            h_FalseCount++; //FalseCount if put back to zero before calling recalc
        }
    }
    else { assert(false); }
}


//make hard functions
//--------------------------------------------------------------
void PureCMS_t::set(const int flipvar) {
    assert(0 == soln->State(flipvar));
    if (1 == soln->State(flipvar)) { return; }

    time_stamp[flipvar] = step; //todo: why keep track of timestamps?
    soln->SetVariable<true>(flipvar);
    unsat_soft(flipvar);
    const auto ClauseCount = v_lits_hard_size[flipvar];

    for (auto i = 0; i < ClauseCount; ++i) {
        const auto Occ = v_lits_hard_clause_id[flipvar][i];
        SetHardLiteralBit(Occ);
        //Unsat literals are tracked in the score calculation
        update_hard_score<us_setvar>(Occ.cls, flipvar);
    } //for hard clauses containing flipvar
    assert(h_MakeScore[flipvar] >= 0); //the flipvar should be tabu
}


//make soft functions
//---------------------------------------------------------
void PureCMS_t::reset(const int flipvar) {
    assert(1 == soln->State(flipvar));
    if (0 == soln->State(flipvar)) { return; }

    time_stamp[flipvar] = step;
    soln->SetVariable<false>(flipvar);
    sat_soft(flipvar);
    const auto ClauseCount = v_lits_hard_size[flipvar];

    for (auto i = 0; i < ClauseCount; ++i) {
        const auto Occ = v_lits_hard_clause_id[flipvar][i];
        ResetHardLiteralBit(Occ);
        //sat/unsat variables are tracked in the score calculations
        update_hard_score<us_resetvar>(Occ.cls, flipvar);
    } //for all hard clauses containing flipvar
}

//initialization
//===============================================================
template <int LogLevel>
void PureCMS_t::initSoln() {
    //first satisfy all literals in all soft clauses, all vars are now 0.
    for (auto cls = 0; cls < sClauseCount; cls++) {
        const auto SatLitCount = s_LiteralsInClauseCount[cls];
        s_SatisfiedLitCount[cls] = SatLitCount;
        //make sure the critical var is set correctly.
        const auto LitCount = s_LiteralsInClauseCount[cls];
        for (uint32_t LitIndex = 0; LitIndex < LitCount; LitIndex++) {
            const auto CurrentVar = s_LiteralsInClauses[cls][LitIndex];
            s_CriticalVarInSoftClause[cls] ^= CurrentVar;
        }
        if (LitCount == 1) {
            //s_BreakScore[s_CriticalVarInSoftClause[cls]] = SoftClauseWeight(cls);
            ChangeSoftBreak(s_CriticalVarInSoftClause[cls], SoftClauseWeight(cls), s_ClauseWeights[cls]);
        }
    }

    //iota(&vars_sorted_by_score[0], &vars_sorted_by_score[nVars+1], 0);
    //all clauses in hard are unsat, add then to the list
    h_FalseCount = 0;
    for (uint32_t cls = 0; cls < hClauseCount; ++cls) {
        //unsat_hard(cls); //put all hClauses into the hard unsat list
        //unsat variables are tracked in the score calculations
        update_hard_score<us_recalc>(cls, 0);
    }

    //from all 0 soln to build.
    //while (h_FalseCount > 0) { //todo: lets see if this is a bottleneck: O(unsat_count * var_count)
    while (h_FalseCount > 0) {
        //stochastically select the best variable to flip
        const auto pick_var = pick_set_var_from_hard_clause(false);
        set(pick_var);
    }
    update_best_soln();
}

void PureCMS_t::SetHardLiteralBit(const Occurrence_t Occ) {
    const auto h_LitIndex = Occ.lit_index;
    const auto cls = Occ.cls;
    h_SatisfiedLitCount[cls]++;
    assert(h_SatisfiedLitCount[cls] <= h_LiteralsInClauseCount[cls]);
    assert(h_LitIndex < h_LiteralsInClauseCount[Occ.cls]);
    assert(h_SATLiteralBits[cls][h_LitIndex / 64][h_LitIndex % 64] == false);
    h_SATLiteralBits[cls][h_LitIndex / 64].set(h_LitIndex % 64);
}

void PureCMS_t::ResetHardLiteralBit(const Occurrence_t Occ) {
    const auto h_LitIndex = Occ.lit_index;
    const auto cls = Occ.cls;
    h_SatisfiedLitCount[cls]--;
    assert(h_SatisfiedLitCount[cls] >= 0);
    assert(h_LitIndex < h_LiteralsInClauseCount[cls]);
    assert(h_SATLiteralBits[cls][h_LitIndex / 64][h_LitIndex % 64] == true);
    h_SATLiteralBits[cls][h_LitIndex / 64].reset(h_LitIndex % 64);
}

bool PureCMS_t::HardLiteralBit(const uint32_t cls, const uint32_t LitPos) const {
    assert(LitPos < h_LiteralsInClauseCount[cls]);
    return h_SATLiteralBits[cls][LitPos / 64][LitPos % 64];
}


void PureCMS_t::reduce_hard_clause_weight() {
    auto sum_weight = 0;
    for (uint32_t cls = 0; cls < hClauseCount; ++cls) {
        h_ClauseWeights[cls] = std::max(1, int(h_ClauseWeights[cls] * p_scale));
        const auto ClauseWeight = h_ClauseWeights[cls];
        const int k_critical = (h_SatisfiedLitCount[cls] - h_K_atLeast[cls]);
        if (k_critical < 0) { //if unsat
            for_each_sat_lit<false>(cls, [=, this](const int LitIndex){
                const auto CurrentVar = h_LiteralsInClauses[cls][LitIndex];
                ChangeHardMake(CurrentVar, h_MakeScore[CurrentVar], ((-k_critical) * ClauseWeight));
            });
        }
        else if (k_critical == 0) { //critically satisfied
            for_each_sat_lit<true>(cls, [=, this](const int LitIndex){
                const auto CurrentVar = h_LiteralsInClauses[cls][LitIndex];
                ChangeHardBreak(CurrentVar, h_BreakScore[CurrentVar], ClauseWeight);
            });
        }
        sum_weight += ClauseWeight;
    }
    avg_weight = sum_weight / hClauseCount;
}

void PureCMS_t::update_clause_weight() {
    //Walk all unsat hard clauses
    for (uint32_t cls = 0; cls < hClauseCount; cls++) { //todo: only loop though false hard clauses, not all hard clauses
        const int k_critical = (h_SatisfiedLitCount[cls] - h_K_atLeast[cls]);
        if (k_critical < 0) { //clause is undersatisfied
            h_ClauseWeights[cls] += 1; //weigh clauses that have been in the clause list for a long time extra
            for_each_sat_lit<false>(cls, [=, this](const int LitIndex) {
                const auto CurrentVar = h_LiteralsInClauses[cls][LitIndex];
                ChangeHardMake(CurrentVar, (-k_critical));
            });
        }
    }

    delta_total_weight += h_FalseCount;
    if (delta_total_weight >= hClauseCount) {
        avg_weight += 1;
        delta_total_weight -= hClauseCount;
    }
    if (avg_weight >= threshold) {
        reduce_hard_clause_weight();
    }
}

//Free move, make a soft clause without breaking any hard clauses
void PureCMS_t::reset_redundent_var_unsatSoft() {
    for (auto item: SoftSortedScores.vars_by_score) {
        const auto test_var = item.var;
        if (0 == h_BreakScore[test_var]) {
            reset(test_var);
        }
    }
}

//-----------------------------------
//Pick an unsatisfied soft clause
template <PickSoftClauseStrategy_t psc>
int PureCMS_t::pick_soft_clause() const {
    auto FalseIndex = (NextFalseIndex > s_FalseCount) ? 0 : NextFalseIndex;
    if (NextFalseIndex > s_FalseCount) { NextFalseIndex = -1; }
    NextFalseIndex++;
    int result;
    switch (psc) {
        case psc_decr: result = FalseIndex; break;
        case psc_incr: result = s_FalseCount - 1 - FalseIndex; break;
        case psc_random: result = rng.NextInt(s_FalseCount); break;
        default: assert(false);
    }
    return result;
}

uint64_t PureCMS_t::GetLargestSoftScore() const {
    auto maxscore = rscore<make_soft>(1);
    for (auto i = 2; i <= nVars; i++) {
        maxscore = max(maxscore, rscore<make_soft>(i));
    }
    return maxscore;
}

//2011
//Adam Lipowski and Dorota Lipowska
//Roulette-wheel selection via stochastic acceptance
int PureCMS_t::pick_soft_var_by_rscore() {
    assert(SoftSortedScores.count() > 0);
    const auto testvar = SoftSortedScores.PickVar(rng, parameters.TopXItemsToPickFrom, parameters.RandomPickPercentage, nullptr); //Stochastically (kind of) pick best out of top 10.
    return testvar;
}

//--------------------------------------
//Pick a hard variable
int PureCMS_t::pick_set_var_from_hard_clause(const bool cc_tabu) {
    const int* tabulist = cc_tabu && (!(tabu_list_size >= SoftSortedScores.count())) ? tabu_remove : nullptr;
    const auto testvar = HardSortedScores.PickVar(rng, parameters.TopXItemsToPickFrom, parameters.RandomPickPercentage, tabulist); //Stochastically (kind of) pick best out of top 10.
    return testvar;
}

//------------------------------------------
// Initialize a new solution for the inner restart
void PureCMS_t::OuterRestart() {

}

//------------------------------------------
// Initialize a new solution for the inner restart
void PureCMS_t::InnerRestart() {
    parameters.MaxInnerRestarts--;

}

void PureCMS_t::write_scores() {
    std::ofstream file;
    file.open("unsat_scores.csv", std::ios::trunc);
    std::stringstream lines;
    lines << "var,score\n";
    for (auto item: SoftSortedScores.vars_by_score) {
        lines << item.var << "," << item.score << std::endl;
    }
    file << lines.str();
    std::cout << lines.str();
    file.close();
}


class my_ofstream: public std::ofstream {
public:
 
    template <typename T>
    void put(const T& data) {
        write((char*)&data, sizeof(T));
    }
};

class my_ifstream: public std::ifstream {
public:
    template <typename T>
    void get(T& data) {
        read((char*)&data, sizeof(T));
    }
};

template <int LogLevel>
bool PureCMS_t::write_state() const {
    const std::string& filename = parameters.OutputFilename;
    my_ofstream file;
    file.open(filename, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file.is_open()) { 
        printf("c Error: File \"%s\" cannot be created\n", filename.c_str());  
        return false;
    }
    file.put("carlsat1"); //header for sanity check
    //file.put(rng); //keep our rng
    file.put(nVars);
    file.put(nClauses);
    file.put(hClauseWeight);

    file.put(best_cost);
    file.put(cost);
    assert(verify());
    best_soln->WriteToStream(file);
#ifdef _DEBUG
    Log(1, "Best assignment has cost: %llu, Popcount: %i, Hash = %i\n", best_soln->cost, best_soln->PopCount(), best_soln->CalculateHash());
#endif

    assert(isEqual(soln->cost, cost));
    soln->FlipCount = this->flipcount;
    //soln->CriticalClauseCount = //not currently used
    soln->WriteToStream(file);
#ifdef _DEBUG
    auto diffsoln = Atom_t::NewHostAtom(nVars, soln->Strategy);
    soln->CloneInto(diffsoln);
    *diffsoln ^= *best_soln;
    Log(0, "Intermediate assignment has cost: %llu, PopCount: %i, Diff popcount = %i, Hash = %i\n", soln->cost, soln->PopCount(), diffsoln->PopCount(), soln->CalculateHash());
    free(diffsoln);
#endif
    //First write all the hard clauses, these have no weight
    file.put(sClauseCount);
    file.put(hClauseCount);
    assert(hClauseCount + sClauseCount == nClauses);
    file.put(h_FalseCount);

    auto check = 0xDEADBEEF;
    file.put(check);
    decltype(h_FalseCount) h_FalseCountCheck = 0;
    for (auto cls = 0; cls < hClauseCount; cls++) {
        file.put(h_LiteralsInClauseCount[cls]);
        file.put(h_SatisfiedLitCount[cls]);
        file.put(h_ClauseWeights[cls]); 
        file.put(h_K_atLeast[cls]);
        h_FalseCountCheck += (h_SatisfiedLitCount[cls] < h_K_atLeast[cls]);
        file.write((char*)h_LiteralsInClauses[cls], sizeof(int) * h_LiteralsInClauseCount[cls]);
        //also write out the current assignment to the cardinality clause
        const auto chunksize = (h_LiteralsInClauseCount[cls] + 63)/64;
        file.write((char*)h_SATLiteralBits[cls], sizeof(int64_t) * chunksize);
    }
    assert(isEqual(h_FalseCount, h_FalseCountCheck));

    file.put(0xCAFEBABE); //make sure data is correctly aligned

    //soft clauses + weights
    file.put(s_LongestClause);
    file.put(s_FalseCount); decltype(s_FalseCount) s_FalseCountCheck = 0;
    decltype(cost) SumClauseWeights = 0;
    decltype(cost) CheckCost = 0;
    for (auto cls = 0; cls < sClauseCount; cls++) {
        file.put(s_LiteralsInClauseCount[cls]);
        file.put(s_SatisfiedLitCount[cls]);
        file.put(s_ClauseWeights[cls]);
        SumClauseWeights += s_ClauseWeights[cls];
        if (0 == s_SatisfiedLitCount[cls]) { CheckCost += s_ClauseWeights[cls]; }
        file.put(s_CriticalVarInSoftClause[cls]);
        s_FalseCountCheck += (isEqual(0u, s_SatisfiedLitCount[cls]));
        file.write((char*)s_LiteralsInClauses[cls], sizeof(int) * s_LiteralsInClauseCount[cls]);
    }
    file.put(SumClauseWeights);
#ifdef _DEBUG
    printf("c write: Sum of clause weights = %llu\n", SumClauseWeights);
#endif
    assert(isEqual(CheckCost, soln->cost));
    assert(isEqual(CheckCost, cost));
    assert(isEqual(s_FalseCount, s_FalseCountCheck));
    file.put(0x8BADF00D); //check alignment at various locations

    //Variable occurrences (which var occurs in what clause?)
    //file.put(v_lits_hard_unsat_count);
    //file.put(v_lits_soft_unsat_count);
    for (auto var = 1; var <= nVars; var++) { //the hard size will be recreated from the source data.
         //file.put(v_lits_hard_kcount[var]);
         //file.put(v_lits_hard_size[var]); //# of hard clauses a var occurs in.
         file.write((char*)v_lits_hard_clause_id[var], sizeof(Occurrence_t) * v_lits_hard_size[var]);
         file.write((char*)v_lits_soft_clause_id[var], sizeof(int) * v_lits_soft_size[var]);
    }

    file.put(0xDEADBABE); //including at the end.
    
    

    file.close();
    Log(2, "c Written assignment with cost %llu\n", cost);
    return true;
}

bool PureCMS_t::read_state() {
    const string filename = parameters.InputFilename;
    my_ifstream file;
    file.open(filename, std::ios::in | std::ios::binary);
        if (!file.is_open()) { 
        printf("c Warning: File \"%s\"does not exist, I'll try and read the problem file\n", filename.c_str());  
        return false;
    }
    char header[sizeof("carlsat1")];
    file.read(header, sizeof(header));
    if (strcmp("carlsat1", header) != 0) { 
        printf("c Fatal Error: File \"%s\" is not a valid CarlSAT state file!\n", filename.c_str());
        exit(-1); //Fatal error: terminate the program.  
        //return false;
    }
    //decltype(rng) dummyrng(0);
    //file.get(dummyrng);   //keep our RNG
    file.get(nVars);
    file.get(nClauses);
    file.get(hClauseWeight);
    allocate();
    initParams();
    file.get(best_cost); 
    file.get(cost);
    //const auto olddegree = degree;
    //const auto oldnVars = nVars;
    best_soln->ReadFromStream(file);
    assert(isEqual(best_soln->cost, best_cost));
    assert(isEqual(best_soln->CalculateHash(), best_soln->Hash));
    soln->ReadFromStream(file);
    assert(isEqual(soln->cost, cost));
    assert(isEqual(soln->CalculateHash(), soln->Hash));

    file.get(sClauseCount);
    file.get(hClauseCount);
    assert(hClauseCount + sClauseCount == nClauses);
    file.get(h_FalseCount);
    uint32_t check = 0;
    file.get(check);
    assert(isEqual(0xDEADBEEF, check));
    assert(h_FalseCount >= 0 && h_FalseCount <= hClauseCount);
    for (auto cls = 0; cls < hClauseCount; cls++) {
        file.get(h_LiteralsInClauseCount[cls]); assert(h_LiteralsInClauseCount[cls] > 0);
        file.get(h_SatisfiedLitCount[cls]); 
        assert(h_LiteralsInClauseCount[cls] <= h_LiteralsInClauseCount[cls]);
        file.get(h_ClauseWeights[cls]); 
        file.get(h_K_atLeast[cls]);
        assert(h_K_atLeast[cls] <= h_LiteralsInClauseCount[cls]);
        h_LiteralsInClauses[cls] = new int[h_LiteralsInClauseCount[cls] + 1];
        file.read((char*)h_LiteralsInClauses[cls], sizeof(int) * h_LiteralsInClauseCount[cls]);
        //also write out the current assignment to the cardinality clause
        const auto ChunkCount = (h_LiteralsInClauseCount[cls] + 63) / 64; 
        h_SATLiteralBits[cls] = new bitset<64>[ChunkCount]();
        file.read((char*)h_SATLiteralBits[cls], sizeof(int64_t) * ChunkCount);
    } //for cls




    file.get(check);
    assert(isEqual(0xCAFEBABE, check));
    
    //soft clauses + weights
    file.get(s_LongestClause);
    InitClauseLengthCorrections();
    const auto olds_ClauseLengthCorrections = s_ClauseLengthCorrections;
    file.get(s_FalseCount);
    
    s_LiteralsInClauseCount = &h_LiteralsInClauseCount[hClauseCount];
    s_LiteralsInClauses = &h_LiteralsInClauses[hClauseCount];
    s_SatisfiedLitCount = &h_SatisfiedLitCount[hClauseCount];
    s_ClauseWeights = &h_ClauseWeights[hClauseCount];
    decltype(cost) SumClauseWeights = 0;
    decltype(cost) CheckCost = 0;
    for (auto cls = 0; cls < sClauseCount; cls++) {
        file.get(s_LiteralsInClauseCount[cls]);
        file.get(s_SatisfiedLitCount[cls]);
        assert(s_SatisfiedLitCount[cls] <= s_LiteralsInClauseCount[cls]);
        file.get(s_ClauseWeights[cls]);
        SumClauseWeights += s_ClauseWeights[cls];
        if (0 == s_SatisfiedLitCount[cls]) { CheckCost += s_ClauseWeights[cls]; }
        file.get(s_CriticalVarInSoftClause[cls]);
        s_LiteralsInClauses[cls] = new int[s_LiteralsInClauseCount[cls] + 1];
        file.read((char*)s_LiteralsInClauses[cls], sizeof(int) * s_LiteralsInClauseCount[cls]);
    }
    decltype(cost) SollSumClauseWeights;
    file.get(SollSumClauseWeights);
#ifdef _DEBUG
    printf("c read: Sum of clause weights = %llu, Should be: %llu, difference: %llu\n", SumClauseWeights, SollSumClauseWeights, SumClauseWeights - SollSumClauseWeights);
#endif
    assert(isEqual(CheckCost, cost));
    assert(isEqual(SumClauseWeights, SollSumClauseWeights));


    file.get(check);
    assert(0x8BADF00D == check);

    auto d = const_cast<int*>(degree);
    total_hard_lits = 0;
    for (uint32_t i = 0; i < hClauseCount; ++i) {
        const auto ClauseLength = h_LiteralsInClauseCount[i];
        for (uint32_t j = 0; j < ClauseLength; ++j) {
            const auto var = h_LiteralsInClauses[i][j];

            d[var] += ClauseLength - 1;
            v_lits_hard_size[var]++;
            v_lits_hard_kcount[var] += h_K_atLeast[i];  //K_atLeast denotes the #of hard clauses merged into one
        }
        total_hard_lits += h_LiteralsInClauseCount[i];
    }

    total_soft_lits = 0;
    for (decltype(sClauseCount) i = 0; i < sClauseCount; ++i) {
        const auto ClauseLength = s_LiteralsInClauseCount[i];
        for (uint32_t j = 0; j < ClauseLength; ++j) {
            const auto var = s_LiteralsInClauses[i][j];
            d[var] += ClauseLength - 1;
            v_lits_soft_size[var]++;
        }
        total_soft_lits += ClauseLength;
    }


    //Variable occurrences (which var occurs in what clause?)
    //file.get(v_lits_hard_unsat_count);
    //file.get(v_lits_soft_unsat_count);
    for (auto var = 1; var <= nVars; var++) {
        //file.get(v_lits_hard_kcount[var]);
        //file.get(v_lits_hard_size[var]); //# of hard clauses a var occurs in.
        v_lits_hard_clause_id[var] = new Occurrence_t[v_lits_hard_size[var] + 1];
        file.read((char*)v_lits_hard_clause_id[var], sizeof(Occurrence_t) * v_lits_hard_size[var]);
#ifdef _DEBUG
        for (auto lit = 0; lit < v_lits_hard_size[var]; lit++) {
            auto occ = v_lits_hard_clause_id[var][lit];
            const auto found = h_LiteralsInClauses[occ.cls][occ.lit_index] == var;
            if (!found) { 
                assert(found); 
            }
        }
#endif

        v_lits_soft_clause_id[var] = new int[v_lits_soft_size[var] + 1];
        file.read((char*)v_lits_soft_clause_id[var], sizeof(int) * v_lits_soft_size[var]);
#ifdef _DEBUG
        for (auto lit = 0; lit < v_lits_soft_size[var]; lit++) {
            const auto cls = v_lits_soft_clause_id[var][lit];
            assert(cls >= 0 && cls < sClauseCount);
            auto found = false;
            for (auto idx = 0; idx < s_LiteralsInClauseCount[cls]; idx++) {
                found = isEqual(s_LiteralsInClauses[cls][idx], var);
                if (found) { break; }
            }
            if (!found) { 
                assert(found); 
            }
        }
#endif
    }

    file.get(check);
    assert(0xDEADBABE == check); //must be the last item in the file
    file.close();


    //Update the soft sorted scores
    SoftSortedScores.clear();
    HardSortedScores.clear();
    assert(SoftSortedScores.count() == 0);
    assert(HardSortedScores.count() == 0);
    //Calculate the make and break scores for variables
    for (auto var = 1; var <= nVars; var++) {
        //Calculate cum cost of unsat soft clauses
        const auto isHardSAT = soln->State(var);
        const auto isSoftSAT = !isHardSAT;
        
        auto s_Cost = 0;
        auto s_PlainCost = 0;
        for (auto idx = 0; idx < v_lits_soft_size[var]; idx++) {
            const auto cls = v_lits_soft_clause_id[var][idx];
            const auto isUNSAT = (0 == s_SatisfiedLitCount[cls]);
            const auto isCritical = ((1 == s_SatisfiedLitCount[cls]) && (isEqual(var, s_CriticalVarInSoftClause[cls])));
            assert(!isCritical || isEqual(s_CriticalVarInSoftClause[cls], var));
#ifdef _DEBUG
            if (isUNSAT)    { assert(!isCritical); }
            if (isCritical) { assert(!isUNSAT);    }
            if (isSoftSAT)  { assert(!isUNSAT);    }
            if (!isSoftSAT) { assert(!isCritical); }
#endif
            const auto soft_cost = SoftClauseWeight(cls);
            const auto plain_cost = s_ClauseWeights[cls];
            //if the var is SAT any clause containing it will be SAT, conversely ...
            //if the var is UNSAT, it can never be the critical variable in a clause
            s_Cost += soft_cost * (isUNSAT || isCritical); //don't worry, it can only be one or the other inside one loop.
            s_PlainCost += plain_cost * (isCritical); //we only track the plain cost for critical clauses 
            assert(!(isUNSAT && isCritical));
        }
        auto softmakes = const_cast<int64_t*>(s_MakeScore);
        softmakes[var] = s_Cost * (!isSoftSAT);

        auto softbreaks = const_cast<int64_t*>(s_BreakScore);
        softbreaks[var] = s_Cost * (isSoftSAT); 
        assert(((softmakes[var] * softbreaks[var]) == 0) && ((softmakes[var] + softbreaks[var]) == s_Cost));
        
        assert(isSoftSAT || (0 == s_PlainCost)); //only track plaincost for critical clauses.
        auto plainbreaks = const_cast<int64_t*>(this->s_PlainBreakScore);
        plainbreaks[var] = s_PlainCost * (isSoftSAT);
        

        //Check for critical hard clauses
        auto h_Cost = 0;
        for (auto idx = 0; idx < v_lits_hard_size[var]; idx++) {
            const auto cls = v_lits_hard_clause_id[var][idx].cls;
            const auto isCritical = isEqual(h_K_atLeast[cls], h_SatisfiedLitCount[cls]) && isHardSAT;
            const auto UnderSATDegree = (h_K_atLeast[cls] - h_SatisfiedLitCount[cls]);
            const auto isUnderSatisfied = (!isHardSAT) && (UnderSATDegree > 0);
            h_Cost += (isCritical * h_ClauseWeights[cls]) + (isUnderSatisfied * UnderSATDegree * h_ClauseWeights[cls]);
            assert(h_ClauseWeights[cls] > 0); 
            assert(isCritical + isUnderSatisfied < 2);
        }
        auto hardbreaks = const_cast<int64_t*>(h_BreakScore);
        hardbreaks[var] = h_Cost * isHardSAT;
        auto hardmakes = const_cast<int64_t*>(h_MakeScore);
        hardmakes[var] = h_Cost * (!isHardSAT); 
        assert((hardmakes[var] + hardbreaks[var]) == h_Cost);
        assert(!(hardmakes[var] > 0 && (hardbreaks[var] > 0)));       
        if (!isSoftSAT) { SoftSortedScores.add(var, rscore<make_soft>(var)); }
        else { HardSortedScores.add(var, rscore<repair_hard>(var)); }
    } //for var

    
#ifdef _DEBUG
    //for (auto var = 0; var <= nVars; var++) {
        //assert(isEqual(unsat_in_soft[var], 0));
        //assert(isEqual(idx_in_unsat_soft[var], -1));
        //assert(isEqual(unsat_in_hard[var], 0));
        //assert(isEqual(idx_in_unsat_hard[var], -1));
        //(isEqual(critical_in_hard[var], 0));
        //assert(isEqual(idx_in_critical_hard[var], 0));
    //}
#endif
    //update unsat_in_soft
    // {
    //     auto idx = 0;
    //     for (auto item: SoftSortedScores.vars_by_score) {
    //         if (-1 == idx_in_unsat_soft[item.var]) {
    //             assert(isEqual(unsat_in_soft[idx], 0));
    //             unsat_in_soft[idx] = item.var;
    //             idx_in_unsat_soft[item.var] = idx++;
    //         }
    //     }
    //     //v_lits_soft_unsat_count = idx;
    //     assert(isEqual(SoftSortedScores.count(), uint32_t(idx))); //SoftSortedScores does not have duplicates
    //     //assert(isEqual(v_lits_hard_unsat_count, 0)); //because we have a valid assignment
    // }
#ifdef _DEBUG
    {
        //auto idx = 0;
        for (auto cls = 0; cls < hClauseCount; cls++) {
            const auto OverSATDegee = h_SatisfiedLitCount[cls] - h_K_atLeast[cls];
            assert(OverSATDegee >= 0); //a valid solution cannot have falsified hard clauses
            //update critical hard vars, this is not currently used by this version, but best to put it in now.
            // if (0 == OverSATDegee) { //Clause is critical
            //     for_each_sat_lit<true>(cls, [&idx, cls, this](const int LitIndex) {
            //         const auto CurrentVar = h_LiteralsInClauses[cls][LitIndex];
            //         if (idx_in_critical_hard[CurrentVar] != -1) {  //prevent duplicates in the list
            //             this->critical_in_hard[idx] = CurrentVar;
            //             idx_in_critical_hard[CurrentVar] = idx++;
            //         }
            //     }); //for every satisfied literal        
            // } //if isCritical
        } //for
    }
#endif
    best_cost = best_soln->cost;
    cost = soln->cost;
    
#ifdef _DEBUG
    assert(verify()); 
#endif
    //we read in the data for the current solution, but we need the best_soln, flip the variables needed to make that happen
    InterpretSoln(best_soln);
#ifdef _DEBUG
    //Make sure all data is internally consistent
    assert(verify());
    assert(0 == h_FalseCount);
    assert(isEqual(cost, best_soln->cost));
#endif
    const int LogLevel = 2;
    Log(2, "Restart from previously saved assignment with cost %llu\n", cost);
    return isEqual(cost, best_soln->cost);
}

template <int LogLevel>
bool PureCMS_t::TrySaveBestSoln() {
    if (h_FalseCount != 0) { return false; }  
    //else { //if all hard clauses satisfied
    reset_redundent_var_unsatSoft();  //make free soft clauses
    update_best_soln(); //save solution if it's better
    Log(2, "new cost after " _lli_ " flips (%.3f secs) is " _lli_ "", flipcount, float(Millisecs(StartTime, now()))/1000.0f, cost);
    return true;
}

//-----------------------------------------------------------------
template <int LogLevel>
void PureCMS_t::doLS() {
    auto cc_tabu = true;
    //auto cc_tabu = false;
    StartTime = now();
    auto EndTime = StartTime;
    int64_t remainingflips = parameters.LoopLengthPhase12;
    flipcount = 0;
    auto LoopCount = 0;
    auto MaxPhaseIFlips = 0;
    auto phaseI_maxloops = parameters.StartPhase1Flips;
    do { //todo: run until timeout
        //Phase 0, see if we have free moves
        if (TrySaveBestSoln<0>()) { //don't log, we'll do that more verbosely below
            no_impr = 0; //reset the improvement counter
            bound_mod = parameters.LoopLengthPhase12;
            phaseI_maxloops = parameters.StartPhase1Flips; 
            Log(2, "new cost after " _lli_ " flips (%.3f secs) is " _lli_ ", phaseI_flips = %i", flipcount, float(Millisecs(StartTime, now()))/1000.0f, cost, phaseI_maxloops);
        } else if (no_impr > bound_mod) {
            //Cache the current best solution
            //Get a new initialization
            remainingflips = parameters.LoopLengthPhase12 * Luby.next();
            if (parameters.MaxInnerRestarts == 0) { /*todo: implement restart*/ }
            if (rng.NextInt(4) == 0) { cc_tabu = !cc_tabu; }
            bound_mod += remainingflips;
            phaseI_maxloops += parameters.IncreasePhase1Flips;
            TrySaveBestToCache<LogLevel>(phaseI_maxloops);
            DoRestart();
            MaxPhaseIFlips = std::max(MaxPhaseIFlips, phaseI_maxloops);
        }

        //Phase I
        auto sum_avg = 0;
        auto phaseI_loopcount = 0;
        while (//(sum_avg < 2.0f * total_hard_lits / nVars) &&
            (s_FalseCount > 0) &&
            (phaseI_loopcount < phaseI_maxloops))
        {
            assert(cost > 0);
            const auto flip_var = pick_soft_var_by_rscore();
            reset(flip_var); //make soft vars
            if (cc_tabu) { tabu_add(flip_var); }
            //sum_avg += v_lits_hard_size[flip_var];
            phaseI_loopcount++;
            phaseI_loopcount *= 2.0f;//1.2f;
            remainingflips--;
            flipcount++;
            if (flipcount >= parameters.maxgens) {
                write_scores();
                return;
            }
            no_impr++;
        } //while


        //Phase II Repair the hard clauses if costs allow for this.
        while (h_FalseCount > 0) { //repair hard clauses if it's not too costly to do so.
            const auto flip_var = pick_set_var_from_hard_clause(cc_tabu); //todo: tweak pick_set_var_from_hard_clause for cardinality clauses
            const auto flipcost = s_PlainBreakScore[flip_var];
            const auto expected_cost = cost + flipcost;
            //allow a range rather than an absolute value.
            //this is needed for the cache to work, because duplicate scores are too rare to work.
            //if ( ! ScoreIsCloseEnough(expected_cost, best_cost, parameters.scoremargin_percentage)) { break; }
            if (expected_cost >= /* >, no too many repeated states */ best_cost) { break; }
            set(flip_var);
#ifdef _DEBUG
            if (0 == SoftSortedScores.last().var) {
                assert(0 != SoftSortedScores.last().var || SoftSortedScores.count() == 0);
            }
            if (expected_cost != cost) {
                assert(expected_cost == cost);
            }
#endif
            remainingflips--;
            flipcount++;
            no_impr++;
        }
        update_clause_weight();
        if (cc_tabu) { clear_tabu(); }
        step++;
        EndTime = now();
        LoopCount++;
    } while (Millisecs(StartTime, EndTime) < parameters.TimeOutInMillisecs);
    soln->cost = this->cost;
    soln->s_FalseCount = this->s_FalseCount;
    soln->time = Millisecs(StartTime, now());
    Log(0, "Loops performed: %i, MaxPhaseIFlips = %i, time taken = %llu ms\n", LoopCount, MaxPhaseIFlips, Millisecs(StartTime, EndTime));
}

template <int LogLevel>
void PureCMS_t::TrySaveBestToCache(const int phaseI_maxloops) { //todo: not yet implemented, cache only ever contains 1 item.
    if (soln->hasImproved) {
        LogIf(1, LogLevel < 2, "new cost after " _lli_ " flips (%.0f ms) is " _lli_ ", phaseI_flips = %i", flipcount, best_soln->time, best_soln->cost, phaseI_maxloops);
        const auto cache_id = Cache->add(this->best_soln).Index();
        switch (cache_id) {
            case duplicate: printf("c item with cost " _lli_ " is duplicate\n", best_soln->cost); break;
            case badscore: printf("c item with cost " _lli_ " is not good enough, needs " _lli_ "", Cache->WorstCost(), best_soln->cost); break;
            default: {
                //printf("c Item #%i with cost " _lli_ " has been written to the cache\n", cache_id, best_soln->cost);
                //printf("c Cache contains: %s\n", Cache->ShowAncestorIDs().c_str());
            }
        };
        soln->hasImproved = false;
    }
}

void PureCMS_t::DoRestart() { //todo: implement restarts (either randomly, or from the cache)
    switch (rng.NextInt(3)) {
        case 0: { //33% get an item from the cache

        }
        case 1: { //33%

        }
        case 2: { //33%

        }
        //default: assert(false); break;
    }
}

void PureCMS_t::InterpretSoln(Atom_t* CachedSoln) {
#ifdef _DEBUG
    Atom_t* DebugSoln = Atom_t::NewHostAtom(soln->VarCount+2, parameters.Strategy); 
    CachedSoln->CloneInto(DebugSoln);
    const auto SollHash = DebugSoln->CalculateHash();
    assert(SollHash == CachedSoln->Hash);
    *DebugSoln ^= *soln; //the population count should now be 0, because they are equal.
    *DebugSoln ^= *soln;
    assert(DebugSoln->CalculateHash() == CachedSoln->CalculateHash());
#endif
    assert(isEqual(cost, soln->cost));
    *CachedSoln ^= *soln; //first store the xor in the current solution, we'll overwrite it later
    //every 1 bit in the soln needs to be flipped.
    //soln[i] ^ cachedsoln[i] will give us the original state back, but we only look at soln[i]==1, so !cachedsoln[i] == old[i].
    //we reuse the code for stepping through cardinality bitsets
    const auto FlipCount = CachedSoln->PopCount();
    auto Index = -1;
    const auto LocateDifferences = true; //all the differences are 1's in CachedSoln
    for (auto i = 0; i < FlipCount; i++) {
        Index = NextSetBit<LocateDifferences>(CachedSoln->AsBitset(), Index);
        const auto OldState = (*soln)[Index];
        if (OldState) { reset(Index); }
        else { set(Index); }
#ifdef _DEBUG
        assert(DebugSoln->State(Index) == soln->State(Index));
#endif
    }
#ifdef _DEBUG
    
    *DebugSoln ^= *soln;
    const auto DiffCount = DebugSoln->PopCount();
    assert(0 == DiffCount);
    decltype(cost) SollCost = 0;
    decltype(s_FalseCount) SollFalseCount = 0;
    for (auto cls = 0; cls < sClauseCount; cls++) {
        if (0 == s_SatisfiedLitCount[cls]) {
            SollFalseCount++;
            SollCost += s_ClauseWeights[cls];
        }
    }
    assert(isEqual(cost, SollCost));
    assert(isEqual(s_FalseCount, SollFalseCount));
    assert(0 == h_FalseCount);
#endif
    //Now that all the internal state matches the cache...
    //CachedSoln->CloneInto(soln); //...overwrite our soln with the cached solution.
    assert(isEqual(cost, CachedSoln->cost));
    /*
        int64_t FlipCount;
        int CriticalClauseCount;
        mutable int Hash;
        bool hasImproved;
        uint32_t AncestorID;
        Strategy_t Strategy;
    */
    soln->FlipCount = CachedSoln->FlipCount;
    soln->CriticalClauseCount = CachedSoln->CriticalClauseCount;
    soln->cost = cost;
    soln->s_FalseCount = s_FalseCount;
    soln->time = CachedSoln->time;
#ifdef _DEBUG
    assert(SollHash == soln->CalculateHash());
#endif
    assert(soln->CalculateHash() == CachedSoln->Hash);
    soln->Hash = CachedSoln->Hash;
    soln->hasImproved = false;
    soln->AncestorID = CachedSoln->AncestorID;
    soln->Strategy = CachedSoln->Strategy; 
    //save the state into best soln. This is esp. important if best_soln == CachedSoln, because we cleared out CachedSoln!
    soln->CloneInto(best_soln);  
}


//==============================================================

float PureCMS_t::getRuntimeInMs() {
    std::chrono::time_point<std::chrono::high_resolution_clock> stop;
    stop = chrono::high_resolution_clock::now();
    auto duration = (stop - StartTime) / 1000; //nanosecs -> microsecs
    return duration.count();
}

template <int LogLevel /*= 0*/>
bool PureCMS_t::verify() const {
    auto result = true;
    for (uint32_t cls = 0; cls < hClauseCount; ++cls) {
        auto sat = false;
        uint32_t SatCount = 0;
        for (uint32_t p = 0; p < h_LiteralsInClauseCount[cls]; p++) {
            const auto var = h_LiteralsInClauses[cls][p];
            if (best_soln->State(var)) {
                sat = (++SatCount >= h_K_atLeast[cls]);
                if (sat) { break; }
            }
        }
        sat |= (0 == h_K_atLeast[cls]);
        if (!sat) {
            Log(0, "Solution is not valid, hard clause %i has satcount %i, needs at least %i", cls, SatCount, h_K_atLeast[cls]);
            result = false;
            return result;
        }
    }
#ifdef _DEBUG
    decltype(h_FalseCount) CheckHFalseCount = 0;
    auto DoubleCheckHFalseCount = h_FalseCount;
    for (auto cls = 0; cls < hClauseCount; cls++) {
        //Check to see if the SATLiteralBits are set correctly.
        auto CheckSATCount = h_SatisfiedLitCount[cls];
        if (CheckSATCount < h_K_atLeast[cls]) { CheckHFalseCount++; }
        auto sat = false;
        uint32_t SatCount = 0;
        for (auto lit = 0; lit < h_LiteralsInClauseCount[cls]; lit++) {
            const auto SATLitState = HardLiteralBit(cls, lit);
            CheckSATCount -= SATLitState;
            const auto CurrentVar = h_LiteralsInClauses[cls][lit];
            if (soln->State(CurrentVar)) {
                sat = (++SatCount >= h_K_atLeast[cls]);
            }
            assert(CurrentVar > 0 && CurrentVar <= nVars);
            const auto VarState = soln->State(CurrentVar);
            assert(SATLitState == VarState);
        }
        sat |= (0 == h_K_atLeast[cls]);
        if (!sat) { DoubleCheckHFalseCount--; }
        assert(0 == CheckSATCount);    
    } //for cls
    assert(isEqual(CheckHFalseCount, h_FalseCount));
    assert(0 == DoubleCheckHFalseCount);

    decltype(cost) check_sum = 0;
    for (auto cls = 0; cls < sClauseCount; cls++) {
        const auto CritVar = s_CriticalVarInSoftClause[cls];
        assert(s_SatisfiedLitCount[cls] <= s_LiteralsInClauseCount[cls]);
        {
            //reconstruct critvar xor stack
            auto XorStack = 0;
            uint32_t SatCount = 0;
            auto clauseweight = s_ClauseWeights[cls];
            for (auto lit = 0; lit < s_LiteralsInClauseCount[cls]; lit++) {
                const auto Var = s_LiteralsInClauses[cls][lit];
                if (false == soln->State(Var)) { 
                    XorStack ^= Var; 
                    SatCount++;
                    clauseweight = 0;
                }
            }            
            check_sum += clauseweight;
            assert(isEqual(XorStack, CritVar));
            assert(isEqual(SatCount, s_SatisfiedLitCount[cls]));
        }
    } //for cls
    assert(isEqual(cost, check_sum));
    assert(isEqual(soln->cost, cost));
#endif
    int SATSoftCount = 0;
    decltype(s_FalseCount) sollFalseCount = 0;
    int64_t soll_sum = 0;
    for (auto cls = 0; cls < sClauseCount; cls++) {
        auto clauseweight = s_ClauseWeights[cls];
        for (uint32_t Lit = 0; Lit < s_LiteralsInClauseCount[cls]; Lit++) {
            const auto CurrentVar = s_LiteralsInClauses[cls][Lit];
            if (best_soln->State(CurrentVar) == 0) { clauseweight = 0; SATSoftCount++; break; } //clause is satisfied, reset its weight
        }
        soll_sum += clauseweight;
        sollFalseCount += (isEqual(0u, s_SatisfiedLitCount[cls]));
    }
    this->SATSoftCount = SATSoftCount;

    LogIf(1, soll_sum == best_cost, "Solution is valid and cost (" _lli_ ") checks out", best_cost);
    LogIf(1, soll_sum != best_cost, "Solution is not valid, reported cost = " _lli_ ", but real cost = " _lli_ "", best_cost, soll_sum);
    assert(isEqual(soll_sum, best_cost));
    assert(isEqual(s_FalseCount, sollFalseCount));
    return result && (soll_sum == (best_cost + unit_fixed_weight));
}

template <int LogLevel>
int MainLoop(PureCMS_t& pCMS) {
    if (!pCMS.hasResumed) {
        pCMS.initSoln<LogLevel>();
    }
    pCMS.doLS<LogLevel>();
    pCMS.TrySaveBestSoln<LogLevel>();
    if (pCMS.doWriteWitness() ) {
        const std::string filename = "witness.txt";
        pCMS.showSoln<LogLevel>(filename);
    } else {
        const auto DoVerification = true;
        const auto DontShowAssignment = false;
        pCMS.showSoln<LogLevel>(DoVerification, DontShowAssignment);
    }
    if (pCMS.hasOutputFile()) {
        //Write the output file to disk
        pCMS.write_state<LogLevel>();
        //pCMS.read_state<LogLevel>(); if you want to test correct writing
    }
    return 0; //todo: add different return codes if needed.
}

int main(const int argc, const char* argv[]) {
    setlocale(LC_ALL, "en_US.utf8");
    //mcheck(nullptr);
    Parameters_t parameters;
    int seed;
    if (argc < 2) {
        //cout << "c use './purems <ins> <seed>' " << endl;
        //exit(0);
        //parameters.SetFilename("/home/johan/Documents/GitHub/CarlSAT/src/challenge.wcnf");
        //parameters.SetFilename("/home/johan/Documents/GitHub/CarlSAT/src/aes-mul_8_3.wcnf");
        parameters.SetFilename("../example.wcard");
        parameters.RandomSeed = 1;
        parameters.TimeOutInMillisecs = 30 * 1000;
    } else {
        if (ParseOptions(argc, argv, parameters) != prSuccess) { exit(0); }
    }

    PureCMS_t pCMS(parameters);
    
    //parameters.show();

    auto exitstatus = 0;
    switch (parameters.LogLevel) {
        case 0: exitstatus = MainLoop<0>(pCMS); break; //make a runtime parameter into a compile-time constant.
        case 1: exitstatus = MainLoop<1>(pCMS); break;
        case 2: exitstatus = MainLoop<2>(pCMS); break;
        default: exitstatus = MainLoop<3>(pCMS); break; //if ppl insist on crazy loglevels, let's give it to them.
    }
    return 0;
}

// //todo: only focus on falsified soft vars: vars that falsify soft clauses.virtual float largest() const { //force disable inlining for debugging purposes
//         if (scores.size() == 0) { return 0.0f; }
//         else { return ExpandScore((*(--scores.cend())).score); }
