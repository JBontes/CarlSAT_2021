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
#include <cstdlib>
#include <cstring>
#include <cfloat>
#include "Macros.hpp"

struct LubySequence_t {
private:
    int64_t u, v;
public:
    LubySequence_t() : u(1), v(1) {}
    int64_t next();
    static bool isValid();
    int DebugGetU() const { return u; }
    int DebugGetV() const { return v; }
    LubySequence_t show() const { return *this; }
};

static_assert(sizeof(LubySequence_t) == (sizeof(int64_t) * 2),"Size mismatch");

int64_t LubySequence_t::next() {
    //method by Knuth, TAOCP volume 4 facsicle 6 satisfiability page 81 Addison-wesley
    const auto result = v;
    //cppcheck-suppress oppositeExpression
    if ((u & -u) == v) { u++; v = 1; } else { v <<= 1; }
    return result;
}

bool LubySequence_t::isValid() {
    bool result = true;
    LubySequence_t test;
    const auto soll = { 1, 1, 2, 1, 1, 2, 4, 1, 1, 2, 1, 1, 2, 4, 8, 1, 1, 2, 1, 1, 2, 4, 1, 1, 2, 1, 1, 2,
            4, 8, 16, 1, 1, 2, 1, 1, 2, 4, 1, 1, 2, 1, 1, 2, 4, 8, 1, 1, 2, 1, 1, 2, 4, 1, 1,
            2, 1, 1, 2, 4, 8, 16, 32, 1 };
    for (auto i : soll) {
        const auto ist = test.next();
        if (ist != i) {
            print("Oops, LubySequence does not work, gives %i, should be" _lli_ "", i, ist);
            assert1(ist == i);
            return false;
        }
    }
    //print("LubySequence in order");
    return result;
}

inline int murmur3_32_scramble(uint32_t k) {
    k *= 0xcc9e2d51;
    k = (k << 15) | (k >> (32-15));
    k *= 0x1b873593;
    return k;
}

int murmur3hash(const int* data, const int datasize) {
    uint32_t h = 0;
    int intsize = datasize / sizeof(int);
    for (auto i = 0; i < intsize; i++) {
        auto k = data[i];
        h ^= murmur3_32_scramble(k);
        h = (h << 13) | (h >> (32-13));
        h = h * 5 + 0xe6546b64;
    }
    //finalize
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

#ifdef MSVC
#pragma warning(disable:4244)
#endif
template <typename T1, typename T2>
__forceinline__ T1 atomXor(T1* data, const T2 mask) {
    static_assert(sizeof(T1) == sizeof(T2) && (4 == sizeof(T1) || 8 == sizeof(T1)), "var must be castible to int32 or int64");
#ifdef MSVC
#pragma warning(suppress:4984)
#endif
    if constexpr (4 == sizeof(T1)) {
        auto old = *((int*)data);
        ((int*)data)[0] ^= mask;
        return old;
    }
#ifdef MSVC
#pragma warning(suppress:4984)
#endif
    if constexpr (8 == sizeof(T1)) {
        auto old = *((int64_t*)data);
        ((int64_t*)data)[0] ^= mask;
        return old;
    }
}
#ifdef MSVC
#pragma warning(default:4244)

#pragma warning(disable:4244)
#endif
template <typename T1, typename T2>
__forceinline__ T1 atomOr(T1* data, const T2 mask) {
    static_assert(sizeof(T1) == sizeof(T2) && (4 == sizeof(T1) || 8 == sizeof(T1)), "var must be castible to int32 or int64");
#ifdef MSVC
#pragma warning(suppress:4984)
#endif
    if constexpr (4 == sizeof(T1)) {
        auto old = *((int*)data);
        ((int*)data)[0] |= mask;
        return old;
    }
#ifdef MSVC
#pragma warning(suppress:4984)
#endif
    if constexpr (8 == sizeof(T1)) {
        auto old = *((int64_t*)data);
        ((int64_t*)data)[0] |= mask;
        return old;
    }
}
#ifdef MSVC
#pragma warning(default:4244)

#pragma warning(disable:4244)
#endif
template <typename T1, typename T2>
__forceinline__ T1 atomAnd(T1* data, const T2 mask) {
    static_assert(sizeof(T1) == sizeof(T2) && (4 == sizeof(T1) || 8 == sizeof(T1)), "var must be castible to int32 or int64");
#ifdef MSVC
#pragma warning(suppress:4984)
#endif
    if constexpr (4 == sizeof(T1)) {
        auto old = *((int*)data);
        ((int*)data)[0] &= mask;
        return old;
    }
#ifdef MSVC
#pragma warning(suppress:4984)
#endif
    if constexpr (8 == sizeof(T1)) {
        auto old = *((int64_t*)data);
        ((int64_t*)data)[0] &= mask;
        return old;
    }
}
#ifdef MSVC
#pragma warning(default:4244)
#endif



struct AtomDontHashThis_t {
    int64_t FlipCount;
    int CriticalClauseCount;
    mutable int Hash;
    bool hasImproved;
    uint32_t AncestorID;
    Strategy_t Strategy;
    constexpr int GetCriticalClauseCount() const { return CriticalClauseCount; }
};

const auto AtomBaseSize = sizeof(AtomDontHashThis_t);

//A bitset for the variable assignments
//The state for non-existing variable 0 is stored as well, just to avoid +1/-1 adjustments
struct Atom_t : public AtomDontHashThis_t {
    enum BoolOp { opXor, opOr, opAnd };
private:
    static const int BitsInInt32 = 32;
    static const int BitsInInt64 = 64;
    static const int BitsInInt256 = 256;
public:   //TODO make private later
    static const int InvalidAtom = -4;
    static const int SmartInitialized = -5;
    static const int RandomlyInitialized = -3;
    static const int AllZeroInitialized = -2;
    static const int AllOneInitialized = -1;
    static const int ValidFalseCount = 0;
public:
    uint32_t VarCount;  //<<-- start hashing from here
    uint32_t s_FalseCount;
    int64_t cost;
    float time;
    union {
        uint32_t raw[1]; //don't worry about alignment, the compiler will not use aligned read/writes anyway.}
        uint64_t raw64[1];
        __m256i avxraw[1];
    };
public:
    Atom_t(const Atom_t&) = delete;
    Atom_t& operator = (const Atom_t&) = delete; //can only allocate Atoms as pointers.

    void WriteToStream(std::ofstream& file) const;
    void ReadFromStream(std::ifstream& file);
    uint32_t GetRaw(const int index) const { return raw[index]; }
    constexpr int GetCost() const { return cost; }
    constexpr static int RawSize(const int VarCount);
    constexpr static int DataSize(const int VarCount); //Datasize is guaranteed to be a multiple of 256 bits
    constexpr static int Int64Size(const int VarCount);
    static Atom_t* NewHostAtom(const int VarCount, const Strategy_t& Strategy);
    int RawSize() const { return Atom_t::RawSize(this->VarCount); }
    void FillWithRandomData(RandomState_t& RandomGenerator);
    void FillAllSame(const InitStrategy_t Value);

    bool State(const uint32_t Var) const;
    bool FlipVariable(const uint32_t Var);
    template <bool newstate>
    void SetVariable(const uint32_t Var);
    bool operator[](const uint32_t variable) const;
    int DataSize() const { return this->DataSize(this->VarCount); }
    int* HashStart() const { return (int*)&this->VarCount; }
    int HashSize() const { return Int64Size(VarCount) - sizeof(AtomDontHashThis_t); }
    int Avx2Count() const { return ((VarCount + (255 + 1)) / BitsInInt256); }
    int IntCount() const { return ((VarCount + (31 + 1/*make provision for dummy var 0*/)) / BitsInInt32); }
    int Int64Count() const { return ((VarCount + (63 + 1)) / BitsInInt64); }
    bool isValid() const { return Atom_t::InvalidAtom != cost; }
    float CalculateScore() const;
    int CalculateHash() const;
    void Print() const;
    void FlipRandomBit(RandomState_t& RandomState);
    int PopCount() const;
    int DiffCount(const Atom_t& b) const;

    std::string ToString(const int FalseCount = 0, const std::string& filename = "") const;
    std::bitset<64>* AsBitset() const { return (std::bitset<64>*)raw64; }

    Atom_t* Clone() const;

    void CloneInto(Atom_t* dest) const;
private:
    __forceinline void GetIndexes(const uint32_t Var, int& start, int& bit) const;
    __forceinline uint64_t GetMask(int& Raw64Index) const;
    template <BoolOp operation>
    __forceinline Atom_t& BoolOpAssign(const Atom_t& b);
public:
    std::string show() const { return format("B:$%llx, H:$%x, L:%i, S:%i, C:%lli", raw64[0], Hash, VarCount, DataSize(), cost); }
    Atom_t& operator^=(const Atom_t& b) { return BoolOpAssign<opXor>(b); }
    Atom_t& operator|=(const Atom_t& b) { return BoolOpAssign<opOr>(b); }
    Atom_t& operator&=(const Atom_t& b) { return BoolOpAssign<opAnd>(b); }

    friend bool operator==(const Atom_t& a, const Atom_t& b);
};

template <Atom_t::BoolOp operation>
Atom_t& Atom_t::BoolOpAssign(const Atom_t& b) {
    const auto count = b.Avx2Count();
    assert1(Avx2Count() == count);
    for (auto i = 0; i < count; i++) {
        switch (operation) {
            case opXor: avxraw[i] = _mm256_xor_si256(this->avxraw[i], b.avxraw[i]); break;
            case opOr:  avxraw[i] = _mm256_or_si256(this->avxraw[i], b.avxraw[i]); break;
            case opAnd: avxraw[i] = _mm256_and_si256(this->avxraw[i], b.avxraw[i]); break;
            default: assert1(false);
        }
    }
    // //Mask off the portion that does not correspond to variables
    // int Raw64Index;
    // const auto mask = GetMask(Raw64Index); 

    // switch (operation) {
    //     case opXor: raw64[Raw64Index] &= ~mask; break;
    //     case opOr:  raw64[Raw64Index] &= ~mask; break;
    //     case opAnd: raw64[Raw64Index] |=  mask; break;
    //     default: assert(false);
    // }
    return *this;
}

float Atom_t::CalculateScore() const {
    if (0 == cost) { return FLT_MAX; }
    else if (cost < 0) { return 0; }
    //const auto Score = 1 / (float)(cost + ((float)CriticalClauseCount / (float)cClauseCount[0]));
    const auto Score = 1.0f / ((float)cost);
    return Score;
}

int Atom_t::CalculateHash() const {
    Hash = murmur3hash(HashStart(), HashSize());
    if (0 == Hash) { Hash = 1; }
    return Hash;
}

int Atom_t::PopCount() const {
    //popcount 64-bit entries
    int result = 0;
    for (auto i = 0; i < Int64Count(); i++) {
        result += popcount64(raw64[i]);
    }
    result -= popcount64(raw64[Int64Count() - 1] & ((((uint64_t)-1) << ((VarCount + 1) % 64)))); //remove tail junk
    return result;
}

int Atom_t::DiffCount(const Atom_t& b) const {
    assert1(this->DataSize() == b.DataSize());
    //popcount 64-bit entries
    int result = 0;
    for (auto i = 0; i < Int64Count(); i++) {
        result += popcount64(raw64[i] ^ b.raw64[i]);
    }
    return result;
}

static const int AtomOverheadSize = sizeof(Atom_t) - sizeof(__m256i);


constexpr int Atom_t::RawSize(const int VarCount) {
    const auto avx2_element_count = ((VarCount + 255) / 256); //the number of 256bit entries.
    return avx2_element_count * (256 / 32) * sizeof(int);
}

constexpr int Atom_t::DataSize(const int VarCount) {
    return RawSize(VarCount) + AtomOverheadSize;
}

constexpr int Atom_t::Int64Size(const int VarCount) {
    const auto int64_element_count = ((VarCount + 63) / 64);
    return (int64_element_count * (64 / 32) * sizeof(int)) + AtomOverheadSize;
}

bool Atom_t::operator[](const uint32_t variable) const {
    const auto result = State(variable);
    return result;
}

std::string Atom_t::ToString(const int FalseCount, const std::string& filename) const {
    std::stringstream result;
    assert1(FalseCount >= 0);
    if (FalseCount > 0) {
        result << "c sorry I was not able to find a satisfying solution \n";
        result << "c " << FalseCount << " falsified clauses left.\n";
    } else { result << "c SAT\n"; }
    if (filename.length() > 0) { result << "c best solution to file: filename\n"; }
    for (decltype(VarCount) i = 0; i < VarCount; i += 32) {
        uint32_t block = raw[i];
        uint32_t a;
        //TODO on the gpu we can parallelize this, note we cannot use string or << there.
        for (auto j = 0; ((a = i + j) < VarCount) && (j < 32); j++) {
            const auto sign = bool(block & 1) ? "-" : "";
            assert1(VarCount > 0);
            const auto comma = a < (VarCount - 1) ? ", " : "\n";
            result << sign << i << comma;
            block >>= 1;
        }
    }
    return result.str();
}

void Atom_t::Print() const {
    for (decltype(VarCount) i = 0; i < VarCount; i += 32) {
        auto block = raw[i];
        uint32_t a;
        for (auto j = 0; ((a = i + j) < VarCount) && (j < 32); j++) {
            const auto sign = bool(block & 1) ? "-" : "";
            assert1(VarCount > 0);
            const auto comma = a < (VarCount - 1) ? ", " : "\n";
            printf("%s%u%s", sign, a, comma);
            block >>= 1;
        } //for j
    } //for i
}

inline Atom_t* Atom_t::Clone() const {
    Atom_t* result = NewHostAtom(this->VarCount, this->Strategy);
    memcpy(result, this, this->DataSize());
    return result;
}

void Atom_t::FlipRandomBit(RandomState_t& RandomState) {
    auto RandomBit = RandomState.NextInt(VarCount);
    FlipVariable(RandomBit);
}

void Atom_t::CloneInto(Atom_t* dest) const {
    memcpy((void*)dest, (void*)this, DataSize());
}

__forceinline void Atom_t::GetIndexes(const uint32_t Var, int& start, int& bit) const {
    //uint32_t index = Var;
    assert1(Var > 0);
    assert1(Var <= VarCount);
    start = Var / 32;
    bit = (1 << (Var % 32));
}

__forceinline uint64_t Atom_t::GetMask(int& Raw64Index) const {
    const auto start = (VarCount + 1) / 64;
    const auto lowmask = ((1 << start) - 1);
    Raw64Index = (VarCount + 1) / 64;
    return ~lowmask;
}

bool Atom_t::FlipVariable(const uint32_t Var) {
    int start, bit;
    GetIndexes(Var, start, bit);
    auto result = atomXor(&raw[start], bit);
    result = ((result ^ bit) & bit); //return result after the flip
    return (0 != result);
}

template <bool newstate>
void Atom_t::SetVariable(const uint32_t Var) {
    int start, bit;
    GetIndexes(Var, start, bit);
    if constexpr (newstate) { //set the variable
        atomOr(&raw[start], bit);
    } else { //reset the variable
        atomAnd(&raw[start], ~bit);
    }
}

void Atom_t::WriteToStream(std::ofstream& file) const {
/**
 * int64_t FlipCount;
    int CriticalClauseCount;
    mutable int Hash;
    bool hasImproved;
    uint32_t AncestorID;
    Strategy_t Strategy;

    uint32_t VarCount;  //<<-- start hashing from here
    int64_t cost;
    float time;
    union {
        uint32_t raw[1]; //don't worry about alignment, the compiler will not use aligned read/writes anyway.}
        uint64_t raw64[1];
        __m256i avxraw[1];
    };
*/
    Hash = CalculateHash();
    file.write((const char*)&FlipCount, sizeof(FlipCount));
    file.write((const char*)&CriticalClauseCount, sizeof(CriticalClauseCount));
    file.write((const char*)&Hash, sizeof(Hash));
    //file.write((char*)&hasImproved, sizeof(hasImproved)); reset this to false on read.
    file.write((const char*)&AncestorID, sizeof(AncestorID));
    file.write((const char*)&Strategy, sizeof(Strategy));
    file.write((const char*)&VarCount, sizeof(VarCount));
    file.write((const char*)&s_FalseCount, sizeof(s_FalseCount));
    file.write((const char*)&cost, sizeof(cost));
    file.write((const char*)&time, sizeof(time));
    file.write((const char*)raw, ((VarCount + 31)/32) * sizeof(int));
#ifdef _DEBUG
  printf("c WriteToStream: cost: %llu, Popcount: %i, FalseCount = %i, hash: %i\n", cost, PopCount(), s_FalseCount, Hash);
#endif
}


void Atom_t::ReadFromStream(std::ifstream& file) {
    file.read((char*)&FlipCount, sizeof(FlipCount));
    file.read((char*)&CriticalClauseCount, sizeof(CriticalClauseCount));
    file.read((char*)&Hash, sizeof(Hash));
    //file.read((char*)&hasImproved, sizeof(hasImproved)); reset this to false on read.
    hasImproved = false;
    file.read((char*)&AncestorID, sizeof(AncestorID));
    file.read((char*)&Strategy, sizeof(Strategy));
    file.read((char*)&VarCount, sizeof(VarCount));
    file.read((char*)&s_FalseCount, sizeof(s_FalseCount));
    file.read((char*)&cost, sizeof(cost));
    file.read((char*)&time, sizeof(time));
    file.read((char*)raw, ((VarCount + 31)/32) * sizeof(int));
#ifdef _DEBUG
    const auto IstHash = CalculateHash();
    assert(isEqual(IstHash, Hash));
    printf("c ReadFromStream: cost: %llu, Popcount: %i, FalseCount = %i, hash: %i\n", cost, PopCount(), s_FalseCount, Hash);
#endif
}

bool Atom_t::State(const uint32_t Var) const {
    int start, bit;
    GetIndexes(Var, start, bit);
    return bool(raw[start] & bit);
}



Atom_t* Atom_t::NewHostAtom(const int VarCount, const Strategy_t& Strategy) {
    auto result = calloc<Atom_t>(Atom_t::DataSize(VarCount));
    result->VarCount = VarCount;
    result->cost = Atom_t::InvalidAtom;
    result->s_FalseCount = -1;
    result->FlipCount = 0;
    result->CriticalClauseCount = -1;
    result->Hash = 0;
    result->hasImproved = true;
    result->Strategy = Strategy;
    return result;
}

void Atom_t::FillWithRandomData(RandomState_t& RandomGenerator) {
    const auto LocalSize = IntCount();
    FlipCount = 0;
    cost = Atom_t::RandomlyInitialized;
    Strategy.InitStrategy = InitRandom;
    for (auto i = 0; i < LocalSize; i++) {
        raw[i] = RandomGenerator.NextInt(UINT32_MAX);
    }
}

void Atom_t::FillAllSame(const InitStrategy_t Value) {
    cost = (int)Value - Atom_t::AllZeroInitialized;
    FlipCount = 0;
    Strategy.InitStrategy = Value;
    auto NumberOfRawInts = this->IntCount();
    auto Filler = (Value == InitFalse) ? 0 : -1;
    for (auto i = 0; i < NumberOfRawInts; i++) {
        raw[i] = Filler;
    }
    Strategy.InitStrategy = Value;
    if (Value == InitTrue) {
        cost = Atom_t::AllOneInitialized;
    } else {
        cost = Atom_t::AllZeroInitialized;
    }
}

bool operator==(const Atom_t& a, const Atom_t& b) {
    //const int AllSame = 0;
    const auto IntCount = a.IntCount();
    if (a.cost == b.cost && Atom_t::InvalidAtom == a.cost) { return true; }
    if (IntCount != b.IntCount()) { return false; }
    //if (a.FalseCount != b.FalseCount) { return false; }
    //cppcheck-suppress unreadVariable
    auto result = true;
#ifdef _DEBUG
    for (auto i = 0; i < IntCount; i++) {
        if (a.raw[i] != b.raw[i]) { result = false; }
    }
#endif
    const auto AvxCount = a.Avx2Count();
    if (AvxCount != b.Avx2Count()) { printif(result, "Atom_t == is incorrect"); assert1(!result); return false; }
    for (auto i = 0; i < AvxCount; i++) {
        const auto packedCompare = _mm256_cmpeq_epi8(a.avxraw[i], b.avxraw[i]);
        const auto bitmask = _mm256_movemask_epi8(packedCompare);
#ifdef _DEBUG
        const auto Equal = -1;
        if ((!result) && (bitmask != Equal)) { printif(result, "Atom_t operator==() is incorrect"); assert1(!result); return false; }
#endif
    }
    assert1(result);
    return true;
}

bool operator!=(const Atom_t& a, const Atom_t& b) {
    return !(a == b);
}
