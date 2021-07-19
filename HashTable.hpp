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
#pragma once
#include <utility>
#include <cstdlib>
#include <cstdint>
#include <array>
#include "DataTypes.hpp"

struct hashindex_t {
    uint32_t data;
    static hashindex_t invalid() { return hashindex_t(-1); }
    constexpr hashindex_t(): data(0) {}
    constexpr explicit hashindex_t(const uint32_t data): data(data) {}
    template <uint32_t N>
    hashindex_t next() { data = ((data + 1) % N); return *this; }
    bool operator<(const hashindex_t rhs) { return data < rhs.data; }
    uint32_t Index() const { return data; }
    bool operator!=(const hashindex_t rhs) const { return data != rhs.data; }
};

/**
 * @brief A hashed value, note that the magic value 'empty=0' means no key.
 */
struct hashkey_t {
private:
    uint32_t data;
public:
    constexpr hashkey_t(): data(0) {}
    constexpr explicit hashkey_t(const uint32_t data): data(data) {}
    constexpr uint32_t Key() const { return data; }
    bool operator==(const hashkey_t& rhs) const { return data == rhs.data; }
    bool operator!=(const hashkey_t& rhs) const { return data != rhs.data; }
};

/**
 * @brief a basic read/write lock, not implemented as yet.
 */
struct Lock_t {
    int data;
};


/**
 * @brief An index into the list holding the data items, scores, etc.
 */
struct linearindex_t {
    int data;
    constexpr linearindex_t(): data(0) {}
    constexpr explicit linearindex_t(const uint32_t data): data(data) {}
    int Index() const { return data; }
    bool operator < (const int rhs) const { return data < rhs; }
    linearindex_t next(const uint32_t limit) { data = ((data + 1) % limit); return *this; }
    linearindex_t add(const uint32_t addition, const uint32_t limit) { data = ((data + addition) % limit); return *this; }
};

/**
 * @brief In the solutions cache we keep atoms and their scores in a linear list.
 * This way can allocate a single block to store assignments without wasting space.
 * Because the cache takes up multiple GBs this matters.
 * A list of atoms, their scores and locations where these item's keys are located in the hash table.
 */
template <uint32_t M>
struct HashTable_t;

template <uint32_t N>
struct linearlist_t {
public:
    RandomState_t RandomGenerator;
    uint32_t maxsize;
    uint32_t count;
    int8_t* datastore;
    float* Scores;   //The score of an item, higher scores are better.
    Atom_t** Atoms;  //compressed assignments
    hashindex_t* IndexIntoHashTable; //an index into the hashtable, where its key can is located, needed when we delete or move an item.
    //Lock_t* Locks;   //A list of multithread locks, one per item.
    //***** for the cache similarity measures
    float CacheSimilarityMeasure;
    float Ex, Ex2, K;
    uint32_t CurrentSize;
    HashTable_t<N>* Parent;
    int64_t BestCost;
    mutable int LastAncestorId; //The most recent AncestorID to be handed out, this is where we'll start off the search for a new empty spot.
    int AncestorIdCounts[N+1]; //track the number of entries in the cache with a given ancestorId, one extra for the Candidate that's in flight.
    static const int MaxFamilyCount = 32;
public:
    uint32_t Count() const { return count; }
    linearlist_t(const uint32_t AtomSize, const uint32_t InitialSize, const float CacheSimilarityMeasure, HashTable_t<N>* Parent)
        : count(0), CacheSimilarityMeasure(CacheSimilarityMeasure), maxsize(N), CurrentSize(InitialSize), RandomGenerator(1), Ex(0), Ex2(0), K(0), Parent(Parent), BestCost(INT64_MAX), LastAncestorId(0)
    {
        memset(AncestorIdCounts, 0, sizeof(AncestorIdCounts));
        Scores = calloc<float>(N);
        Atoms = calloc<Atom_t*>(N);
        IndexIntoHashTable = calloc<hashindex_t>(N);
        //Locks = calloc<Lock_t>(N);
        datastore = (int8_t*)malloc(AtomSize * N);
        for (uint32_t i = 0; i < N; i++) { Atoms[i] = (Atom_t*)&datastore[AtomSize * i]; }
    }

    ~linearlist_t() {
        free(datastore);
        free(IndexIntoHashTable);
        free(Scores);
        //free(Locks);
        free(Atoms);
    }
    linearindex_t add(const Atom_t& SourceAtom, const float Score, const hashindex_t HashIndex) { //todo: remove items from the hashtable proper.
        auto Overfull = (Count() == CurrentSize);
        if (Overfull && (SourceAtom.cost >= BestCost)) { //Check to see if items are too self-similar
            auto BestCount = 0;
            for (auto i = 0; i < Count(); i++) {
                const bool isAlike = ScoreIsCloseEnough(Atoms[i]->cost, BestCost, CacheSimilarityMeasure);
                if (isAlike) { BestCount++; }
            }

            //If all cached items have the same false count, double the cache
            if (Count() == BestCount) { CurrentSize = std::min(CurrentSize * 2, maxsize); }
            Overfull = (Count() == CurrentSize);
        }
        BestCost = std::min(BestCost, SourceAtom.cost);
        auto DestIndex = count;

        if (Overfull) { //evict the worst item
            //Check the number of family members of the new candidate.
            assert1(SourceAtom.AncestorID < N);
            const auto FamilyCount = AncestorIdCounts[SourceAtom.AncestorID];
            const auto EvictFamilyId = (FamilyCount >= MaxFamilyCount) ? SourceAtom.AncestorID : -1;
            auto WorstScore = FLT_MAX;
            auto WorstIndex = -1;
            for (auto i = 0; i < Count(); i++) {
                if ((Scores[i] < WorstScore)
                  //if the
                   && (EvictFamilyId == -1 || EvictFamilyId == Atoms[i]->AncestorID)) { WorstIndex = i; WorstScore = Scores[i]; }
            }
            assert1(-1 != WorstIndex);
            if (Score <= WorstScore) { return linearindex_t(-1); }
            DestIndex = WorstIndex;
            const auto HashKey = Parent->InternalIndexToKey(IndexIntoHashTable[DestIndex]);
            VarianceRemoveItem(Scores[DestIndex]);
            AncestorIdCounts[Atoms[DestIndex]->AncestorID]--;
            assert1(AncestorIdCounts[Atoms[DestIndex]->AncestorID] >= 0);
            Parent->InternalRemove(HashKey, *Atoms[DestIndex]);
        }
        else { //not overfull, just add it
            count++;
        }
        assert1(Count() <= CurrentSize);
        SourceAtom.CloneInto(Atoms[DestIndex]);
        Scores[DestIndex] = Score;
        VarianceAddItem(Score);
        AncestorIdCounts[SourceAtom.AncestorID]++;
        IndexIntoHashTable[DestIndex] = HashIndex;
        return linearindex_t(DestIndex);
    }

    hashindex_t remove(const linearindex_t index) {
        const auto RemoveAncestorId = Atoms[index.Index()]->AncestorID;
        AncestorIdCounts[RemoveAncestorId]--;
        assert1(AncestorIdCounts[RemoveAncestorId] >= 0);

        if (index.Index() == (int)(--count)) {
            return hashindex_t::invalid();
        } else { //Put the last item into the removed item
            Atoms[index.Index()] = Atoms[count];
            Scores[index.Index()] = Scores[count];
            auto result = IndexIntoHashTable[count];
            IndexIntoHashTable[index.Index()] = result;
            return result;
        }
    }

    void move(const linearindex_t MovedIndex, const hashindex_t dest) {
        //items are always moved into an empty spot, so we only need to worry about a single datapoint
        IndexIntoHashTable[MovedIndex.Index()] = dest;
    }

    linearindex_t GetStochasticallyWorstItem() const;
    int NextAncestorId() const;
    float GetVariance() const { return (Ex2 - (Ex * Ex) / Count()) / Count();/* - 1, no we have the total population*/ }
    float GetMean() const { return K + (Ex / Count()); }
    float GetStandardDeviation() const { return sqrtf(GetVariance()); }


    void VarianceAddItem(const float NewScore);
    void VarianceRemoveItem(const float OldScore);
    void VarianceReplaceItem(const float OldScore, const float NewScore);
};

template <uint32_t N>
void linearlist_t<N>::VarianceAddItem(const float NewScore) {
    assert1(Count() > 0);
    if (Count() == 1) { K = NewScore; }
    Ex += (NewScore - K);
    Ex2 += (NewScore - K) * (NewScore - K);
}

template <uint32_t N>
void linearlist_t<N>::VarianceRemoveItem(const float OldScore) {
    Ex -= (OldScore - K);
    Ex2 -= (OldScore - K) * (OldScore - K);
}

template <uint32_t N>
void linearlist_t<N>::VarianceReplaceItem(const float OldScore, const float NewScore) {
    VarianceRemoveItem(OldScore);
    VarianceAddItem(NewScore);
}

template <uint32_t N>
int linearlist_t<N>::NextAncestorId() const {
    auto start = 0;
    for (auto i = 0; i < Count(); i++) {
        start = (LastAncestorId + i) % Count();
        if (0 == AncestorIdCounts[start]) {
            LastAncestorId = start;
            break;
        }
    }
    return start;
}

    static const auto duplicate = -2;
    static const auto badscore = -1;

/**
 * @brief A single fixed size linear probe hashtable, the only purpose of the hash table is to test for duplicate assignments
 * to test this we add atoms (compressed assignments) to the hashtable. If they insert, fine. If not, then they're duplicate and
 * need to be discarded.
 *
 * @tparam M the number of atoms (compressed assignments) that can be stored in the cache.
 */
template <uint32_t M>
class HashTable_t {
    static const uint32_t N = M * 4; //the number of entries in the hashtable

    const hashkey_t empty = hashkey_t(0);
    //const float OnePercent = 1.0f/100;
    hashkey_t* keys;
    linearindex_t* LinearIndexes;
    linearlist_t<M> AtomData;
    mutable RandomState_t Random;
    float BestScore, WorstScore;
public:
    HashTable_t(const uint32_t AtomSize, const uint32_t InitialSize, const float scoremargin_percentage)
        : AtomData(AtomSize, InitialSize, scoremargin_percentage, this), WorstScore(FLT_MAX), BestScore(FLT_MIN), Random(1)
    {
        assert2(empty.Key() == 0, "calloc init of the hashkeys assumes empty == 0");
        keys = calloc<hashkey_t>(N);
        LinearIndexes = calloc<linearindex_t>(N);
        //LinearIndexes = (linearindex_t*)malloc(N * sizeof(linearindex_t));
        Random = RandomState_t(1);
    }

    ~HashTable_t() {
        free(keys);
        free(LinearIndexes);
    }
    int Count() const { return AtomData.Count(); }
    int DebugInitCount() const { return AtomData.initcount; }
    void CloneAtom(const linearindex_t index, Atom_t& dest) const { AtomData.Atoms[index.Index()]->CloneInto(&dest); }
    Atom_t& ReadOnlyAtom(const linearindex_t index) const { return *AtomData.Atoms[index.Index()]; }
    float Score(const linearindex_t index) const { return AtomData.Scores[index.Index()]; }
    //add an item and return index if it's unique, silently reject and return (negative) error code otherwise
    linearindex_t add(const Atom_t* Atom);
    void remove(const hashkey_t key, const Atom_t& item); //remove the given item; fail an assert if the item is not in the cache.
    hashindex_t InternalRemove(const hashkey_t key, const Atom_t& item);
    uint32_t count() const { return AtomData.Count(); }
    void StochasticallyCopyBestAssignmentIntoAtom(Atom_t& dest) const;
    void StochasticallyRemoveWorstAssignment();
    bool isEntriesTooAlike() const;
    float EntryDiffPercentage() const;
    int64_t BestCost() const;
    int64_t AverageCost() const;
    int64_t WorstCost() const;
    int NextAncestorId() const;
    bool isFull() const { return Count() == this->AtomData.CurrentSize; }
    hashkey_t InternalIndexToKey(const hashindex_t Index) const { return keys[Index.Index()]; }
    Atom_t* GetBestAssignment() const;
    std::string ShowAncestorIDs() const;
private:
    hashindex_t find(const hashkey_t key, const Atom_t& item, bool& found) const; //find the given item and return its index (found = TRUE), or return an empty slot to put the item into (found = FALSE).
    void CloneAtom(const hashindex_t index, Atom_t& dest) const { linearindex_t li = LinearIndexes[index.Index()]; CloneAtom(li, dest); }
    const Atom_t& ReadOnlyAtom(const hashindex_t index) const {
        linearindex_t li = LinearIndexes[index.Index()];
        if (li.Index() < Count()) {
            assert2(li < Count(), "li = %i >= Count=%i", li.Index(), Count());
        }
        return *AtomData.Atoms[li.Index()];
    }
    float Score(const hashindex_t index) const { return AtomData.Scores[LinearIndexes[index.Index()].Index()]; }
    linearindex_t KeyToLinear(const hashindex_t index) const { return LinearIndexes[index.Index()]; }
    hashindex_t KeyToIndex(const hashkey_t key) const { hashindex_t result; result.data = (key.Key() % N); return result; }
};

template <uint32_t M>
int64_t HashTable_t<M>::AverageCost() const {
    int64_t TotalCost = 0;
    if (Count() == 0) return INT32_MAX;
    for (auto i = 0; i < Count(); i++) {
        TotalCost += AtomData.Atoms[i]->cost;
    }
    return (TotalCost / Count());
}

template <uint32_t M>
int64_t HashTable_t<M>::WorstCost() const {
    if (!isFull()) { return INT64_MAX; }
    int result = 0;
    for (auto i = 0; i < Count(); i++) {
        result = (AtomData.Atoms[i]->cost > result) ? AtomData.Atoms[i]->cost : result;
    }
    return result;
}

template <uint32_t M>
int HashTable_t<M>::NextAncestorId() const {
    return this->AtomData.NextAncestorId();
}

template <uint32_t M>
int64_t HashTable_t<M>::BestCost() const {
    return AtomData.BestCost;
}

template <uint32_t M>
hashindex_t HashTable_t<M>::find(const hashkey_t key, const Atom_t& item, bool& found) const {
    found = false;
    auto index = KeyToIndex(key);
    while (true) {
        while (empty != keys[index.Index()] && key != keys[index.Index()]) { index.template next<N>(); } //skip over non-empty_non-matching keys.
        if (empty == keys[index.Index()]) { return index; } //because the hashtable is only 1/4 full, it will always find an empty key and exit.
        else { //check to see if item exists
            const auto& StoredAtom = ReadOnlyAtom(index);
            if (StoredAtom == item) { found = true; return index; } //never compare pointers
            else { index.template next<N>(); } //try to find the next matching key
        }
    }
}

// template <uint32_t M>
// linearindex_t HashTable_t<M>::add(const hashkey_t key, const Atom_t& item, const float score) {
//     assert1(key != const_cast<hashkey_t>(empty));
// #ifdef _DEBUG
//     const Atom_t* TestForNullRef = &item;
//     assert1(TestForNullRef != nullptr);
// #endif
//     bool found;
//     auto hashindex = find(key, item, found);
//     if (found) { return linearindex_t(-1); } //do not insert true duplicates
//     else {
//         AtomData.add(item, score, hashindex);
//         return LinearIndexes[hashindex.Index()];
//     }
// }

template <uint32_t M>
linearindex_t HashTable_t<M>::add(const Atom_t* Atom) {
    assert1(Atom != nullptr);
    assert1(Atom->hasImproved);

    const auto score = Atom->CalculateScore();
    auto Overfull = (Count() == AtomData.CurrentSize); //Higher scores are better
    BestScore = (score > BestScore) ? score : BestScore;
    const auto key = hashkey_t(Atom->CalculateHash());
    assert1(key != empty);

    bool found;
    auto index = find(key, *Atom, found);
    if (found) { return linearindex_t(duplicate); } //do not insert true duplicates
    else {
        const auto LinIndex = AtomData.add(*Atom, score, index);
        if (LinIndex.Index() >= 0) { //If the addition succeeded update the hashtable
            keys[index.Index()] = key;
            LinearIndexes[index.Index()] = LinIndex;
        }
        return LinIndex;
    }
}

template <uint32_t M>
hashindex_t HashTable_t<M>::InternalRemove(const hashkey_t key, const Atom_t& item) {
    const auto index = [&](){ //do not allow index to change after a match has been found
        auto index = KeyToIndex(key);
    #ifdef _DEBUG
        auto found = false;
        while (empty != keys[index.data] && key != keys[index.data] && item != ReadOnlyAtom(index)) {
            index.template next<N>();
            found = (item != ReadOnlyAtom(index));
        }
    #else
        while (empty != keys[index.data] && key != keys[index.data] && item != ReadOnlyAtom(index)) { index.template next<N>(); }
    #endif
        return index;
    }();
    keys[index.Index()] = empty;
    //Look through all the subsequent non-empty keys, to see if any are out of place.
    hashindex_t run = index; run.next<N>();
    hashindex_t gap = index;
    auto old = keys[run.data];
    while (empty != old) { //check all the entries in a run to see if the key is in the wrong place, runs end at a empty entry.
        const auto soll = KeyToIndex(old);
        if (soll != run) { //try to put the key in the correct place
            auto warparound = (run < gap); //if we've circled around, adjust accordingly
            if ((soll.data + (warparound * N)) <= gap.Index()) { //this key will fit better in the gap, move it
                keys[gap.Index()] = keys[run.Index()];
                keys[run.Index()] = empty;
                LinearIndexes[gap.Index()] = LinearIndexes[run.Index()];
                LinearIndexes[run.Index()].data = -50; //not strictly neccesary, but let us track deletions in debug.
                AtomData.move(LinearIndexes[gap.Index()], gap); //report the move to the linear list
                gap = run;
            }
        }
        old = keys[run.next<N>().data];
    } //while
    return index;
}

template <uint32_t M>
void HashTable_t<M>::remove(const hashkey_t key, const Atom_t& item) {
    auto index = InternalRemove(key, item);
    AtomData.remove(LinearIndexes[index.Index()]);
}

template <uint32_t M>
void HashTable_t<M>::StochasticallyCopyBestAssignmentIntoAtom(Atom_t& dest) const {
    //Start at a random starting point
    const auto Start = Random.NextInt(Count());
    //assert1(Count() >= 32);
    const uint32_t PrefixLen = 32+1;
    std::array<float, PrefixLen> ScorePrefixsum;
    ScorePrefixsum[0] = 0.0;
    float ScoreSum = 0;
    for (auto i = 0; i < 32; i++) {
        const auto s = AtomData.Scores[i % Count()];
        ScoreSum += s;
        ScorePrefixsum[i + 1] = ScoreSum;
    }
    const auto Best = (BestOutOfN(ScorePrefixsum, Random.NextFloat(ScoreSum)) + Start) % Count();
#ifdef _DEBUG
    // //Ensure that BestOutOfN performs OK for all possible scenarios.
    // for (auto i = 0; i < 32; i++) {
    //     assert1(BestOutOfN(ScorePrefixsum, ScorePrefixsum[i]) == (i + 1));
    //     assert1(BestOutOfN(ScorePrefixsum, (ScorePrefixsum[i] + ScorePrefixsum[i+1]) / 2) == (i + 1));
    // }
    // assert1(BestOutOfN(ScorePrefixsum, ScorePrefixsum[32]) == 32);
#endif
    AtomData.Atoms[Best]->CloneInto(&dest);
}

template <uint32_t M>
void HashTable_t<M>::StochasticallyRemoveWorstAssignment() {
    //Todo: add implementation
}

template <uint32_t M>
bool HashTable_t<M>::isEntriesTooAlike() const {
    return (AtomData.GetStandardDeviation() / AtomData.GetMean()) < AtomData.CacheSimilarityMeasure;
}

template <uint32_t M>
float HashTable_t<M>::EntryDiffPercentage() const {
    return (AtomData.GetStandardDeviation() / AtomData.GetMean()) * 100;
}

template <uint32_t M>
Atom_t* HashTable_t<M>::GetBestAssignment() const {
    auto result = 0;
    auto Best = INT32_MAX;
    const auto Start = Random.NextInt(Count());
    for (auto i = 0; i < Count(); i++) {
        const auto index = (Start + i) % Count();
        if (BestCost() == AtomData.Atoms[index]->cost) { return AtomData.Atoms[index]; }
        const auto FalseCount = AtomData.Atoms[index]->FalseCount;
        if (FalseCount < Best) {
            result = index;
            Best = FalseCount;
        }
    }
    return AtomData.Atoms[result];
    printf("c Best cost in cache = %i\n", AtomData.Atoms[result]->FalseCount);
}

template <uint32_t M>
std::string HashTable_t<M>::ShowAncestorIDs() const {
    std::stringstream result;
    for (auto i = 0; i < Count(); i++) {
        char InitChar;
        switch (AtomData.Atoms[i]->Strategy.InitStrategy) {
            case InitTrue: InitChar = '+'; break;
            case InitFalse: InitChar = '-'; break;
            case InitSmart: InitChar = '*'; break;
            case InitRandom: InitChar = '?'; break;
            default: assert1(false);

        }
        result << InitChar << AtomData.Atoms[i]->AncestorID << ":" << AtomData.Atoms[i]->cost << " ";
    }
    return result.str();
}



