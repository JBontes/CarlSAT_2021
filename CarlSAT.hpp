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

#include <iostream>
#include <cstdlib>
#include <fstream>
#include <string>
#include <set>
#include <limits.h>
#include <cmath>
#include <random>
#include <chrono>
#include <bitset>
#include <string.h>
#include <assert.h>
#include "FilePreprocess.hpp"
#include "Macros.hpp"
#include "DataTypes.hpp"
#include "HashTable.hpp"
using namespace std;

enum updatescore_t {us_setvar, us_resetvar, us_recalc};
enum usetabu_t { ut_usetabu, ut_avoidtabu };
enum usecc_t { uc_usecc, uc_avoidcc };
enum PickSoftClauseStrategy_t { psc_incr, psc_decr, psc_random };
enum Phase_t { make_soft, repair_hard };

#ifdef __APPLE__
float frac(const float value) { return value - trunc(value); }
#else
float constexpr frac(const float value) { return value - trunc(value); }
#endif

struct BestScores_t {
private:
    float Ex, Ex2, K;  //for calculating the average and deviation
    void VarianceAddItem(const float NewScore) {
        assert(count() > 0);
        if (count() == 1) { K = NewScore; }
        Ex += NewScore - K;
        Ex2 += (NewScore - K) * (NewScore - K);
    }

    void VarianceRemoveItem(const float OldScore) {
        Ex -= OldScore - K;
        Ex2 -= (OldScore - K) * (OldScore - K);
    }

public:
    struct score_t {
        float score;
        int var;
        score_t(const int var, const float score): var(var), score(score) {}
        bool operator<(const score_t rhs) const { return (score < rhs.score) || ((score == rhs.score)  && (var < rhs.var)); }
        bool operator<(const float rhs) const { return score < rhs; }
    };

    /*multi*/set<score_t> vars_by_score;
    float cum_score;
    BestScores_t(): vars_by_score(), cum_score(0) {}
    uint32_t count() const { return vars_by_score.size(); }
    score_t last() const { if (count() == 0) { return score_t(0,0); } else { return *(--vars_by_score.cend()); } }

    void add(const int var, const float rscore) {
        assert(var > 0);
        if (rscore <= 0) { return; }
        vars_by_score.insert(score_t(var, rscore));
        cum_score += rscore;
    }

    void remove(const int var, const float rscore) {
        assert(var > 0);
        if (rscore <= 0) { return; }
        assert(count() > 0);
        assert(vars_by_score.contains(score_t(var, rscore)));
        vars_by_score.erase(score_t(var, rscore));
        cum_score -= rscore;
    }

    float GetVariance() const { return (Ex2 - (Ex * Ex) / count() / count()/* - 1, no we have total population*/); }
    float GetMean() const { return K + (Ex / count()); }
    int PickVar(RandomState_t& rng, const uint32_t topn, const uint32_t RandomPickPercentage, const int* tabulist);

};

#pragma GCC diagnostic push //take out these pragmas after experiments are done.
#pragma GCC diagnostic ignored "-Wcomment" //putting /**/ inside comments here is fine
//do not feed in tabu list if # of tabu vars == ## of pickvars
int BestScores_t::PickVar(RandomState_t& rng, const uint32_t topn, const uint32_t RandomPickPercentage, const int* tabulist) {
    //only look at the top n items.
    const auto r = rng.NextInt(100);
    const auto minitem = std::min(count(), topn);
    if (r < RandomPickPercentage) { //pick a random var
        const int randpick = (rng.NextInt(minitem) + 1);
        auto item = vars_by_score.cend();
        advance(item, -randpick);
        return item->var;
    } else { //rest of the time do a stochastic pick
        const auto maxscore = last().score;

        float minscore;
        {
            auto middle = vars_by_score.cend();
            for (auto i = 0; i < minitem; i++) {
                assert(middle != vars_by_score.cbegin());
                middle--;
            }
            minscore = middle->score;
        }
        assert(minscore <= maxscore);
        const auto StartPoint = rng.NextFloat(maxscore - minscore) + minscore;
        assert(StartPoint <= maxscore);
        auto Start = vars_by_score.lower_bound(score_t(0, StartPoint)); //O(log n)
        assert(Start != vars_by_score.cend());
        const auto StartScore = Start->score;
        auto result = Start->var;
        if (nullptr == tabulist) {
            while (true) {
                Start++;
                //if (past end                   or no longer duplicate  bow out
                if ((vars_by_score.end() == Start || StartScore != Start->score || (rng.NextInt(3) < 1))) { return result; }
                // with 66% probability keep looking for another option
                else {
                    result = Start->var;
                    //odds of getting stuck in this loop = (0.66)^n = 1,5% after 10 tries and 0.025% after 20 tries.
                }
            }
        } else {
            while (true) {
                result = Start->var;
                const auto var_is_tabu = (tabulist[result] > 0);
                //move to the beginning, selecting worse scores
                if (!var_is_tabu) { return result; }
                else if (vars_by_score.cbegin() == Start) {
                    return result;
                } else { Start--; }
            }
        }
        assert(false); //we should never get here.
        return result; //but, disable warning
    } /*else do stochastic pick*/
}
#pragma GCC diagnostic pop

using timepoint = chrono::high_resolution_clock::time_point;

struct Occurrence_t {
    uint32_t cls;
    uint32_t lit_index;
    Occurrence_t(uint32_t cls, uint32_t lit_index): cls(cls), lit_index(lit_index) {}
    explicit Occurrence_t(): cls(0), lit_index(0) {}
};

class PureCMS_t {
private:
    //  all vars are indexed from 1;
    //  and clauses are from 0.
    uint32_t       nVars;
    uint32_t       nClauses;
    mutable RandomState_t rng;
    int64_t        flipcount;
    mutable uint32_t NextFalseIndex;
    uint64_t       step;
    bool           lit_in_hClause_is_positive; //to remove var sense in the problem.
    double         cutoff_time;
    uint64_t       total_hard_lits;
    uint64_t       total_soft_lits;
    Parameters_t   parameters;
    LubySequence_t Luby;
    HashTable_t<1024>* Cache;
    timepoint_t    StartTime;


    //random numbers
    uint32_t rand(const uint32_t max = UINT32_MAX) const;
    float    rand(const float max) const;

    //info hard clauses;
    uint64_t     hClauseWeight;
    uint32_t     hClauseCount;
    int**        h_LiteralsInClauses;            //[cls][idx]
    uint32_t*    h_LiteralsInClauseCount;
    uint32_t*    h_SatisfiedLitCount;            //sat literal num in clauses;
    uint64_t*    h_ClauseWeights;                //weight of clauses
    //New for cardinality clauses
    uint32_t*    h_K_atLeast;                    //Cardinality clauses, the number of literals needed to satisfy.
    bitset<64>** h_SATLiteralBits;             //bitset with all the satisfied literals

    //info soft clauses, all soft clauses are normal SAT clauses
    int          sClauseCount;                    //#of soft clauses
    uint32_t     s_LongestClause;                 //Longest soft clause
    int**        s_LiteralsInClauses;             //[cls][literalIdx] Lits in clauses
    uint32_t*    s_LiteralsInClauseCount;         //[cls]ClauseLengths
    uint32_t*    s_SatisfiedLitCount;             //[cls]#of sat literals in clause 0..ClauseLength
    int*         s_CriticalVarInSoftClause;       //[cls]Which var in a clause with 1 sat literal is satisfied?
    uint64_t*    s_ClauseWeights;                 //[cls]soft weights
    int*         s_ClauseLengthCorrections;       //[cls]multiplication factor for clauseweights due to clause length
    int          SoftClauseWeight(const int cls) { return s_ClauseWeights[cls] * s_ClauseLengthCorrections[s_LiteralsInClauseCount[cls]]; }

    //info about vars;
    //int**        v_lits_hard_clause_id;  // [var][#] clauses number;
    int**        v_lits_soft_clause_id;    // [var][#] Occurrences for the soft clauses
    Occurrence_t** v_lits_hard_clause_id;  // [var][#] Occurrences for the hard clauses
    //uint32_t**   v_lits_pos_hard;        // [var][cls] position of a literal in a hard clause //for cardinality clauses only

    int**        v_lits_hard_neighbor;   // neighbor for var in cls;
    int*         v_lits_hard_size;       // [var] # of hard clauses a variable occurs in
    int*         v_lits_soft_size;       // [var] # of soft clauses a variable occurs in
    int          v_lits_soft_unsat_count;// #vars that falsify soft clauses
    int          v_lits_hard_unsat_count;// #vars that falsify hard clauses
    int*         v_lits_hard_kcount;     // [var] hardsize * k_atleast, for use in heuristics
    BestScores_t SoftSortedScores;       // soft variables sorted by rscore
    BestScores_t HardSortedScores;       // hard variables sorted by rscore


    //soln.
    //soln[var_idx] = 0(false) / 1(true)
    int64_t      cost;                   //the sum of unsat soft sat clauses weight
    int64_t      best_cost;              //best weight
    int64_t      unit_fixed_weight;      //weight fixed in up
    Atom_t*      soln;
    int64_t*     time_stamp;
    Atom_t*      best_soln;
    float        best_time;
    uint64_t     sum_cweight;            //total of the clause weights
    uint64_t     no_impr;                //# of runs without improvement in the score
    uint64_t     bound_mod;              //incremental upbound modifier
    inline void update_best_soln();

    int64_t const*    s_BreakScore;       //[var] cum. break weight for soft clauses per variable
    int64_t const*    s_PlainBreakScore;  //[var] cum. unadjusted cost delta if soft clauses are broken.
    int64_t const*    s_MakeScore;        //[var] cum. make weight for soft clauses per variable (for critical vars only)
    int64_t const*    h_BreakScore;       //[var] cum. break weight for hard clauses per variable
    int64_t const*    h_MakeScore;        //[var] cum. make weight for hard clauses per variable (for critical vars only)
    int const*        degree;              //[var] # of variables that share a clause with this var.
    //scores
    //vscore = sum(now uncovered w(e)) - ( +-node{v} uncovered w(e));
    // now      next
    // 0    ->  1       : dscore>=0
    // 1    ->  0       : dscore<=0
    //int*    dscore;
    //int*    valid_score;
    template <Phase_t Phase>
    inline float rscore(const uint32_t var) const {
        assert(var > 0 && var <= nVars);
        float result;
        switch (Phase) {
            case make_soft:   result = s_MakeScore[var] / (float)(h_BreakScore[var] + parameters.hard_epsilon); break;
            case repair_hard: result = h_MakeScore[var] / (float)(s_BreakScore[var] + parameters.soft_epsilon); break;
            default: assert(false); break;
        }
        return result;
    }


    //soft rscore = soft_make / hard_break
    void ChangeSoftMake(const int var, const int delta) {
        const auto oldrscore = rscore<make_soft>(var);
        SoftSortedScores.remove(var, oldrscore);
        auto sms = const_cast<int64_t*>(s_MakeScore);
        const auto oldscore = sms[var];
        const auto newscore = oldscore + delta;
        sms[var] = newscore;
        assert(!(oldscore == 0 && newscore < 0));
        assert(oldscore >= 0);
        assert(newscore >= 0);
        assert(delta != 0);
        if (oldscore == 0) { //add variable to the falsified list
            assert(newscore > 0);
            unsat_in_soft[v_lits_soft_unsat_count] = var;
            idx_in_unsat_soft[var] = v_lits_soft_unsat_count++;
        }
        else if (newscore == 0) { //remove variable from the falsified list
            assert(oldscore > 0);
            const auto tailvar = unsat_in_soft[--v_lits_soft_unsat_count];
            const auto holeidx = idx_in_unsat_soft[var];
            unsat_in_soft[holeidx] = tailvar;
            idx_in_unsat_soft[tailvar] = holeidx;
        }
        const auto newrscore = rscore<make_soft>(var);
        SoftSortedScores.add(var, newrscore);
    }

    //soft rscore = soft_make / hard_break
    void ChangeHardBreak(const int var, const int delta) {
        const auto oldscore = rscore<make_soft>(var);
        SoftSortedScores.remove(var, oldscore);
        auto hbs = const_cast<int64_t*>(h_BreakScore);
        hbs[var] += delta;
        assert(h_BreakScore[var] >= 0);
        const auto newscore = rscore<make_soft>(var);
        SoftSortedScores.add(var, newscore);
    }

    void ChangeHardBreak(const int var, const int Old, const int New) {
        assert(Old == h_BreakScore[var]);
        ChangeHardBreak(var, -Old + New);
    }

    //hard rscore = hard_make / soft_break
    void ChangeHardMake(const int var, const int delta) {
        const auto oldrscore = rscore<repair_hard>(var);
        HardSortedScores.remove(var, oldrscore);
        auto hms = const_cast<int64_t*>(h_MakeScore);
        const auto oldscore = hms[var];
        const auto newscore = oldscore + delta;
        hms[var] = newscore;
        assert(delta != 0);
        assert(newscore >= 0);
        assert(oldscore >= 0);
        if (oldscore == 0) { //literal falsifies one or more hard clauses
            //assert(newscore > 0);
            unsat_in_hard[v_lits_hard_unsat_count] = var;
            idx_in_unsat_hard[var] = v_lits_hard_unsat_count++;
        }
        else if (newscore == 0) { //literal does not falsify any hard clause
            //assert(oldscore > 0);
            const auto tailvar = unsat_in_hard[--v_lits_hard_unsat_count];
            const auto holeidx = idx_in_unsat_hard[var];
            unsat_in_hard[holeidx] = tailvar;
            idx_in_unsat_hard[tailvar] = holeidx;
        }
        //assert(h_MakeScore[var] >= 0);
        const auto newrscore = rscore<repair_hard>(var);
        HardSortedScores.add(var, newrscore);
    }

    void ChangeHardMake(const int var, const int Old, const int New) {
        assert(Old == h_MakeScore[var]);
        ChangeHardMake(var, -Old + New);
    }

    //hard rscore = hard_make / soft_break
    void ChangeSoftBreak(const int var, const int weighteddelta, const int plaindelta) {
        const auto oldrscore = rscore<repair_hard>(var);
        HardSortedScores.remove(var, oldrscore);
        auto sbs = const_cast<int64_t*>(s_BreakScore);
        sbs[var] += weighteddelta;
        assert(s_BreakScore[var] >= 0);
        const auto newrscore = rscore<repair_hard>(var);
        HardSortedScores.add(var, newrscore);

        auto spbs = const_cast<int64_t*>(s_PlainBreakScore);
        spbs[var] += plaindelta;
    }

    //unsat clauses of hard clauses
    int*         unsat_in_hard; //[var]
    int*         critical_in_hard; //[var]
    int          h_FalseCount;
    int*         idx_in_unsat_hard; //[var]
    int*         idx_in_critical_hard; //[var]
    //void         sat_hard(const int idx);   //clause idx, pop
    //void         unsat_hard(const int idx); //push
    void         SetHardLiteralBit(const Occurrence_t Occ);
    void         ResetHardLiteralBit(const Occurrence_t Occ);
    inline bool  HardLiteralBit(const uint32_t cls, const uint32_t LitPos) const;
    template <bool satlit, typename functor_t>
    void         for_each_sat_lit(const int ClauseIndex, functor_t functor);
    template <bool satlit>
    int          Get_Nth_sat_lit(const uint32_t cls, const uint32_t n) const;
    template <updatescore_t updatekind>
    void         update_hard_score(const int cls, const int flipvar);

    //false var in soft
    uint32_t    s_FalseCount;
    int*        unsat_in_soft;             //[var]
    int*        idx_in_unsat_soft;         //[var]
    mutable int SATSoftCount;              //# of satisfied soft clauses, for reporting purposes only
    inline void sat_soft(const int var);   //var idx, pop from stack
           void unsat_soft(const int var); //walk through all the soft clauses containing the var, update unsat list, critical list and update the scoring


    //tabu to remove.
    int*        tabu_remove;    //0 can rm, 1 cannot rm.
    int*        tabu_list;      //store tabu_vars;
    int         tabu_list_size;
    inline void tabu_add(const int var);
    inline void clear_tabu();
    //int         tabu_var; not used.

    //smooth
    int         avg_weight;
    uint32_t    delta_total_weight;
    int         threshold;
    float       p_scale;


    //------------------------------------------
    template <int LogLevel = 0>
    bool    verify() const;  //verify the soln.
    void    allocate(); //allocate memory for data structures.
    void    initParams(); //initialize all parameter.
    float  getRuntimeInMs(); //get time

    //var sense 1->0
    void    reset(const int var);

    //var sense 0->1
    void    set(const int var);

    //if var set 0 or 1 do nothing to the hard clauses, set 0.
    inline  void reset_redundent_var_nVars();
    inline  void reset_redundent_var_unsatSoft();
    //inline  void reset_redundent_and_rmCand();
    //smooth weight
    void    update_clause_weight();
    void    reduce_hard_clause_weight();

    //pick a soft clause
    template <PickSoftClauseStrategy_t psc>
    int pick_soft_clause() const;

    //pick var methods
    int        try_num;   //BMS

    uint64_t GetLargestSoftScore() const;
    inline int pick_soft_var_by_rscore();
    template <usetabu_t usetabu, usecc_t usecc>
    int pick_soft_var_from_clause(const int ClauseIndex);

    int        try_pick;                //used in pick set var;
    inline int pick_set_var_from_hard_clause(const bool cc_tabu);

    //---------------------------
    //int*    vars_sorted_by_score;
    //============================
public:
    PureCMS_t() = delete;
    PureCMS_t(const Parameters_t& parameters): rng(parameters.RandomSeed), SoftSortedScores(), HardSortedScores() {
        if (!build(parameters.show())) { exit(-1); };
        Cache = new HashTable_t<1024>(this->soln->DataSize(), 1, parameters.scoremargin_percentage);
    }

    ~PureCMS_t() {
        freeMemory();
    }
    //build from instance
    bool    build(const Parameters_t& parameters);
    //print soln following requirement.
    template <int LogLevel>
    void    showSoln(bool needVerify) const;
    template <int LogLevel>
    void    showSoln(const string& filename) const;
    //the local search process, which need call build first.
    void    InnerRestart();
    void    OuterRestart();
    template <int LogLevel>
    void    doLS();
    //initialize the solution by greedy means
    template <int LogLevel>
    void    initSoln();
    void    freeMemory();

    //Caching functions
    template <int LogLevel>
    void TrySaveBestToCache(const int phaseI_maxloops);
    void DoRestart();
    void InterpretSoln(const Atom_t* CachedSoln);
};