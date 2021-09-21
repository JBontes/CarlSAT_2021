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


//This is a collection of bits and bops used all over the place
#pragma once
#ifdef MSVC
    #include <intrin.h>
#else
    #include <cstdint>
    #include <x86intrin.h>
#endif
#include <immintrin.h>
#include <avx2intrin.h>
#include <cstdio>
#include <type_traits>
#include <iterator>
#include <string>
#include <string_view>
#include <chrono>
#include <assert.h>
#include <iomanip>
#include <utility>
#include <cstdarg>
#include <bit>
#include <inttypes.h>

#include <clocale>


#ifdef _MSC_VER
    #define MSVC
#else
    #ifdef __GNUC__
        #define GCC
    #endif
#endif

//#define NDEBUG
#ifndef NDEBUG
    #define _DEBUG
#else
    #undef _DEBUG
#endif

#define __forceinline inline
#define __forceinline__ __forceinline
typedef unsigned int uint32_t;
typedef int int32_t;
#ifdef __unix__
    #define unix
#endif
#ifdef unix
    #define _lli_ "%'li"
    #define _llu_ "%'lu"
#else
    #define _lli_ "%'lli"
    #define _llu_ "%'llu"
#endif
#ifdef _MSC_VER
    #define __restrict__ __restrict
#endif

#include <iostream>

//Print the string representation of a type at compile time
//https://stackoverflow.com/a/56766138/650492
template <typename T>
constexpr auto type_name() noexcept {
    std::string_view name = "Error: unsupported compiler", prefix, suffix;
    #ifdef __clang__
        name = __PRETTY_FUNCTION__;
        prefix = "auto type_name() [T = ";
        suffix = "]";
    #elif defined(__GNUC__)
        name = __PRETTY_FUNCTION__;
        prefix = "constexpr auto type_name() [with T = ";
        suffix = "]";
    #elif defined(_MSC_VER)
        name = __FUNCSIG__;
        prefix = "auto __cdecl type_name<";
        suffix = ">(void) noexcept";
    #endif
    name.remove_prefix(prefix.size());
    name.remove_suffix(suffix.size());
    return name;
}

//Get the varname of a variable
//https://stackoverflow.com/a/38697366/650492
#define VARNAME(variable) ((void)variable, #variable)

//https://stackoverflow.com/a/38237385/650492
constexpr const char* GetFilename(const char* path) {
    const char* result = path;
    while (*path) {
        const auto c = *path++;
#ifdef MSVC
        if ('/' == c || '\\' == c) { result = path; }
#else
        if ('/' == c) { result = path; }
#endif
    }
    return result;
}

//Simple logging that takes up zero runtime if the loglevel is not verbose enough
static const int LogAlways = 0;
static const int LogNormal = 1;
static const int LogVerbose = 2;
static const int LogVeryVerbose = 3;

//Must have a compile time constant (or templated) 'LogLevel 0..3' in scope
#define Log(AskedLogLevel, format, ...) \
static_assert((uint32_t)(LogLevel) <= 3, "LogLevel out of bounds"); \
static_assert((uint32_t)(AskedLogLevel) <= 3, "Requested loglevel out of bounds"); \
do { if constexpr ((LogLevel) >= (AskedLogLevel)) { \
    printf("c " format "\n", ## __VA_ARGS__); \
}} while(0)

#define LogIf(AskedLogLevel, Predicate, format, ...) \
static_assert((uint32_t)(LogLevel) <= 3, "LogLevel out of bounds"); \
static_assert((uint32_t)(AskedLogLevel) <= 3, "Requested loglevel out of bounds"); \
do { if constexpr ((LogLevel) >= (AskedLogLevel)) { \
    if ((Predicate)) { printf("c " format "\n", ## __VA_ARGS__); } \
}} while(0)

#define alwaysprintif(predicate, format, ...) do { if(predicate) { printf("CPU Line %i: " format "\n", __LINE__, ## __VA_ARGS__); } } while (0)
#define alwaysprint(format, ...) do { printf("Line %i:" format "\n", __LINE__, ## __VA_ARGS__); } while (0)
#define alwaysprintonce(format, ...) do { printf("Line %i" format "\n", __LINE__, ## __VA_ARGS__); } while (0)

//#define NOPRINT
#ifdef NOPRINT
#define printn(format, ...) do {} while (0)  //do nothing
#define print(format, ...) do {} while (0)  //do nothing
#define printonce(format, ...) do {} while (0) //do nothing
#define printwarp(format, ...) do {} while (0) //do nothing
#define printif(predicate, format, ...) do {} while (0) //do nothing
#else
#ifdef _DEBUG
#define printn(format, ...) do { fprintf(stderr, (format), ## __VA_ARGS__); } while (0)
#define print(format, ...) do { fprintf(stderr, (format), ## __VA_ARGS__); fprintf(stderr,"\n"); } while (0)
#define printonce(format, ...) do { fprintf(stderr, (format), ## __VA_ARGS__); fprintf(stderr,"\n"); } while (0)
#define printonceif(predicate, format, ...) do { if (predicate) { fprintf(stderr, (format), ## __VA_ARGS__); fprintf(stderr,"\n");} } while (0)
#define printwarp(format, ...) do { fprintf(stderr, (format), ## __VA_ARGS__); fprintf(stderr,"\n"); } while (0)
#define printwarpif(predicate, format, ...) do { if (predicate) { fprintf(stderr, (format), ## __VA_ARGS__); fprintf(stderr,"\n");} } while (0)
#define printif(predicate, format, ...) do { if (predicate) { fprintf(stderr, (format), ## __VA_ARGS__); fprintf(stderr,"\n");} } while (0)

#else
#define printn(format, ...) do {} while (0)  //do nothing
#define print(format, ...) do {} while (0)  //do nothing
#define printonce(format, ...) do {} while (0) //do nothing
#define printonceif(predicate, format, ...) do {} while (0) //do nothing
#define printwarp(format, ...) do {} while (0) //do nothing
#define printwarpif(predicate, format, ...) do {} while (0) //do nothing
#define printif(predicate, format, ...) do {} while (0) //do nothing
#endif

#endif

#ifndef NDEBUG
    #define assert1(test) do { if(!(test)) { printf("CPU: %s assert failed in line %i and file %s\n", __FUNCTION__, __LINE__, GetFilename(__FILE__)); }} while (0)
    #define assert2(test, format, ...) do { if(!(test)) { printf("CPU: %s assert failed in line %i and file %s\n" format "\n", __FUNCTION__, __LINE__, GetFilename(__FILE__), ## __VA_ARGS__); }} while (0)
#else
    #define assert1(test) do {} while (0)
    #define assert2(test, format, ...) do {} while (0)
#endif

#define __noinline__ __attribute__((noinline))

#define Bin "%c%c%c%c%c%c%c%c"
#define ToBin(byte)  \
    (byte & 0x80 ? '1' : '0'), \
    (byte & 0x40 ? '1' : '0'), \
    (byte & 0x20 ? '1' : '0'), \
    (byte & 0x10 ? '1' : '0'), \
    (byte & 0x08 ? '1' : '0'), \
    (byte & 0x04 ? '1' : '0'), \
    (byte & 0x02 ? '1' : '0'), \
    (byte & 0x01 ? '1' : '0')


template <typename NumericType>
struct ioterable {
    using iterator_category = std::input_iterator_tag;
    using value_type = NumericType;
    using difference_type = NumericType;
    using pointer = typename std::add_pointer<NumericType>::type;
    using reference = NumericType;

    explicit ioterable(NumericType n) : val_(n) {}

    ioterable() = default;
    ioterable(ioterable&&) = default;
    ioterable(ioterable const&) = default;
    ioterable& operator=(ioterable&&) = default;
    ioterable& operator=(ioterable const&) = default;

    ioterable& operator++() { ++val_; return *this; }
    ioterable operator++(int) { ioterable tmp(*this); ++val_; return tmp; }
    bool operator==(ioterable const& other) const { return val_ == other.val_; }
    bool operator!=(ioterable const& other) const { return val_ != other.val_; }

    value_type operator*() const { return val_; }

private:
    NumericType val_{ std::numeric_limits<NumericType>::max() };
};

template <typename T>
T* calloc(const uint32_t elementcount) {
    //cppcheck-suppress
    return (T*)calloc(elementcount, sizeof(T));
}

// template <typename T>
// T* calloc(const uint64_t elementcount) {
//     return (T*)calloc(elementcount, sizeof(T));
// }

template <typename T>
constexpr uint32_t BitCount() { return sizeof(T) * 8; }

template <typename T>
constexpr bool isEqual(const T& a, const T& b) { return a == b; } //just in case both sides of a comparison are mutable.

template <typename T>
constexpr bool notEqual(const T& a, const T& b) { return a != b; } //just in case both sides of a comparison are mutable.

constexpr int BoolToPlusMinus(const bool Flag) { return (1 ^ -Flag) + Flag; } //https://graphics.stanford.edu/~seander/bithacks.html#ConditionalNegate




static const int exitcode_sat = 10;
static const int exitcode_unsat = 20;
static const int exitcode_unknown = 0;

int LogSolutionSat() { printf("s SATISFIABLE\n"); return exitcode_sat; }
int LogSolutionUnknown() { printf("s UNKNOWN\n"); return exitcode_unknown; }



//https://stackoverflow.com/a/38697366/650492
#define VARNAME(variable) ((void)variable, #variable)

auto now() {
    auto result = std::chrono::high_resolution_clock::now();
    return result;
}

using timepoint_t = std::chrono::high_resolution_clock::time_point;

uint64_t Microsecs(const timepoint_t start, const timepoint_t end) {
    uint64_t result = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    return result;
}

uint32_t Seconds(const timepoint_t start, const timepoint_t end) {
    uint32_t result = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
    return result;
}

uint64_t Millisecs(const timepoint_t start, const timepoint_t end) {
    uint64_t result = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    return result;
}

std::string timestring(const uint64_t musecs) {
    std::stringstream result;
    const auto micros = musecs % 1'000'000;
    auto secs = musecs / 1'000'000;
    auto mins = secs / 60;
    auto hours = mins / 60;
    secs %= 60;
    mins %= 60;
    if (hours > 0) { result << std::setw(2) << std::setfill('0') << hours << "H:"; }
    if (mins > 0 || hours > 0)  { result << std::setw(2) << std::setfill('0') << mins << "M:"; }
    result << std::setw(2) << std::setfill('0') << secs << "S:";
    result << std::setw(6) << std::setfill('0') << musecs << "mu";
    return result.str();
}

uint64_t FlipsPerSec(const uint64_t musecs, const uint64_t flips) {
    double secs = (double)musecs / 1'000'000;
    double result = (double)flips / secs;
    return (uint64_t)result;
}

//Thanks Peter
//https://stackoverflow.com/a/36491672/650492
__m256i bitmap_to_epi32vectormask(uint8_t mask) {
    const auto OneBytePerBitMask = 0x8080808080808080llu; //every byte has sign bit set.
    const uint64_t zero = 0;
    const auto OneBytePerBit = _pdep_u64((uint64_t)mask, OneBytePerBitMask); //do not run this on Ryzen1 !
    const __m128i xmm0 = _mm_cvtsi64_si128(OneBytePerBit);
    const auto result = _mm256_cvtepi8_epi32(xmm0);
    return result;
}

//https://stackoverflow.com/a/48726371/650492
uint32_t mm256_extract_epi32_var_indx(const __m256i& vec, const uint32_t i )
{
    assert1(i <= 7);
    const auto indx = _mm_cvtsi32_si128(i);
    const auto val  = _mm256_permutevar8x32_epi32(vec, _mm256_castsi128_si256(indx));
    return _mm_cvtsi128_si32(_mm256_castsi256_si128(val));
}

//Prefix scan of 8 floats in an ymm register.
//https://stackoverflow.com/a/19519287/650492
template <uint32_t shiftleft>
inline __m256 _mm256_scan_AVX2(__m256 x, const uint32_t ClauseLength) {
    //shift1_AVX + add
    auto t0 = _mm256_permute_ps(x, _MM_SHUFFLE(2, 1, 0, 3));
    auto t1 = _mm256_permute2f128_ps(t0, t0, 41);
    x = _mm256_add_ps(x, _mm256_blend_ps(t0, t1, 0x11));

    if (ClauseLength > 2) {
        //shift2_AVX + add
        t0 = _mm256_permute_ps(x, _MM_SHUFFLE(1, 0, 3, 2));
        t1 = _mm256_permute2f128_ps(t0, t0, 41);
        x = _mm256_add_ps(x, _mm256_blend_ps(t0, t1, 0x33));
    }
    if (ClauseLength > 4) {
        //shift3_AVX + add
        x = _mm256_add_ps(x,_mm256_permute2f128_ps(x, x, 41));
    }
    static_assert(shiftleft <= 1, "");
    if constexpr (shiftleft == 1) {
        t0 = _mm256_permute_ps(x, _MM_SHUFFLE(2, 1, 0, 3));
        t1 = _mm256_permute2f128_ps(t0, t0, 41);
        x = _mm256_blend_ps(t0, t1, 0x11);
    }
    return x;
}

/*
template <typename T>
struct mm256_t {
    static_assert(sizeof(T) == 4 || sizeof(T) == 8, "");
    __m256i ymm;
    mm256_t(const __m256i& input): ymm(input) {}
    T data() const {
        static_assert(sizeof(T) == 4, "");
        if constexpr(sizeof(T) == 4) { auto a = _mm256_cvtsi256_si32(ymm); return (T)a; }
        //else { return (T)_mm256_cvtsi256_si64(ymm); }
    }
    T operator[](const uint32_t index) const { assert1(index <= 7); T result; result.SetRaw(mm256_extract_epi32_var_indx(ymm, index)); return result; }
};
*/

//Winner!, faster than the binary search.
//Check to see if a value falls between two values in the prefixsum.
//A candidate to replace a binary search.
int ValueBetween(__m256 prefixsum, const float value) {
    const auto valuevector = _mm256_set1_ps(value);
    auto ge = (__m256i)_mm256_cmp_ps(prefixsum, valuevector, _CMP_GE_OQ); //0 0 0 0 1 1 1 1
    auto lt = (__m256i)_mm256_cmp_ps(prefixsum, valuevector, _CMP_LT_OQ); //1 1 1 1 0 0 0 0
    auto bitsetge = _mm256_movemask_epi8(ge) >> 4; //movemask_epi8 converts a 32-bit -1 into 4 bits.
    auto bitsetlt = _mm256_movemask_epi8(lt);
    auto overlap = bitsetge & bitsetlt;
    return (_tzcnt_u32(overlap) / 4); //so we shift and divide by 4 to get the proper index.
}

//using ProbReal_t = double; //see if we can get away with using singles for the probability calculationm
using ProbReal_t = float;

/*
@inbook{inbook,
author = {Howes, Lee and Thomas, David},
year = {2007},
month = {08},
pages = {},
title = {Efficient Random Number Generation and Application Using CUDA}
}
*/
//store state data for random number generation, we use this randomgenerator because it uses very few registers, unlike CUDA's other randomgenerators
//Do not swap this random generator unless you want to break compatability with BallotSAT.
struct RandomState_t {
private:
    unsigned z1, z2, z3, z4;
    void init(const unsigned t1, const unsigned t2, const unsigned t3, const unsigned l4) {
        z1 = (t1 | (128 * (t1 < 128))); //Tausworthe init values z1,z2,z3 should be > 128.
        z2 = (t2 | (128 * (t1 < 128))); //Force this outcome.
        z3 = (t3 | (128 * (t1 < 128)));
        z4 = l4;
    }

    // S1, S2, S3, and M are all constants, and z is part of the
    // private per-thread generator state.
    unsigned TausStep(unsigned& z, const int S1, const int S2, const int S3, const unsigned M) {
        unsigned b = (((z << S1) ^ z) >> S2);
        return z = (((z & M) << S3) ^ b);
    }

    //Linear Congruential Generator
    // A and C are constants
    unsigned LCGStep(unsigned& z, const unsigned A, const unsigned C) { return z = (A * z + C); }
public:
    //RandomState_t() = delete;
#ifdef __CUDA_ARCH__
    __device__ explicit RandomState_t(uint32_t tid) {
        curandState s;
        curand_init(ThreadId() ^ __brev(clock()), 0, 0, &s);  //force clock data into the high bits
        //draw four numbers and init from there
        init(curand(&s), curand(&s), curand(&s), curand(&s));
    }
#else
    explicit RandomState_t(uint32_t) { init(std::rand(), std::rand(), std::rand(), std::rand()); } //todo: base this on the ns clock.
    explicit RandomState_t(const uint32_t z1, const uint32_t z2, const uint32_t z3, const uint32_t z4): z1(z1), z2(z2), z3(z3), z4(z4) {
         this->z1 = (z1 | (128 * (z1 < 128))); this->z2 = (z2 | (128 * (z2 < 128))); this->z3 = (z3 | (128 * (z3 < 128)));
    }
#endif
    unsigned NextInt(const unsigned Max); //A uniform random generator 0 <= R < Max
    uint64_t NextInt64(const uint64_t Max);
    __forceinline int NextInt(const int Max) { return (int)NextInt(unsigned(Max)); }
    __forceinline int NextInt64(const int64_t Max) { return (int64_t)NextInt64(uint64_t(Max)); }
    ProbReal_t NextFloat(); //A uniform random generator 0 <= R < 1
    ProbReal_t NextFloat(ProbReal_t max);
private:
    uint32_t Random();
};

static_assert(sizeof(RandomState_t) == sizeof(int) * 4, "Size mismatch");

uint32_t RandomState_t::Random() {
    // Combined period is lcm(p1,p2,p3,p4)~ 2^121
    return (              // Periods
        TausStep(z1, 13, 19, 12, 4294967294UL) ^  // p1=2^31-1
        TausStep(z2, 2, 25, 4, 4294967288UL) ^    // p2=2^30-1
        TausStep(z3, 3, 11, 17, 4294967280UL) ^   // p3=2^28-1
        LCGStep(z4, 1664525, 1013904223UL)        // p4=2^32
        );
}

//A uniform random generator 0 <= R < Max
unsigned RandomState_t::NextInt(const unsigned Max) {
    const auto result = Random();
    return result % Max;
}

uint64_t RandomState_t::NextInt64(const uint64_t Max) {
    return (((uint64_t(Random())) << 32) || (uint64_t(Random()))) % Max;
}

#ifdef MSVC
#pragma warning(disable:4244)
#endif
//A uniform random generator 0 < R < 1
ProbReal_t RandomState_t::NextFloat() {  //__device__ __host__ float HybridTaus(RandomState_t& state)
    const ProbReal_t result = 2.3283064365387e-10f * Random();
    assert(result >= 0 && result < 1);
    return result;
}
#ifdef MSVC
#pragma warning(default:4244)

#pragma warning(disable:4244)
#endif
//A uniform random generator 0 < R < 1
ProbReal_t RandomState_t::NextFloat(ProbReal_t max) {  //__device__ __host__ float HybridTaus(RandomState_t& state)
    // Combined period is lcm(p1,p2,p3,p4)~ 2^121
    const ProbReal_t result = 2.3283064365387e-10f * Random() * max;
    assert(result >= 0);
    assert(result <= max);
    return result;
}
#ifdef MSVC
#pragma warning(default:4244)
#endif

enum BreakWeightStrategy_t { bws_Inverse = 0, bws_Uniform = 1, bws_Linear = 2, bws_Exponential = 3  };
enum CachedStrategy_t { cs_CreateNew, cs_GetFromCache };
enum StrategyProbSAT_t { sps_PowersOf2, sps_UseProbSAT };
enum InitStrategy_t {InitFalse = 0, InitMin = InitFalse, InitRandom, InitTrue, InitSmart, InitMax = InitTrue/*InitSmart*/, InitNone=4 }; //make sure that InitZero and


struct Strategy_t {
    struct stratlimits_t {
        int index, min, max, def;
    };
    static constexpr auto StratCount = 5;
    static constexpr auto MaxStratValue = 7;
    struct winnercounts_t {
        int count[StratCount][MaxStratValue+1];
    };
    static constexpr auto MaxBreakWeightMultiplier = 7;
    static constexpr stratlimits_t limits[StratCount] = {
    /*cached*/      { 0, cs_CreateNew, cs_GetFromCache, cs_GetFromCache },  //draw from cache, create new instance
    /*correct*/     { 1, sps_PowersOf2, sps_UseProbSAT, sps_UseProbSAT },  //0=use doubling breakweights, 1=use probsat
    /*pol*/         { 2, InitMin, InitMax, InitRandom }, //all neg, random, all pos, smart
    /*weight*/      { 3, 1,MaxBreakWeightMultiplier,5 },  //Min and max factors for the BreakWeights
    /*BreakWeight*/ { 4, bws_Inverse, bws_Linear, bws_Linear }}; //exclude bws_Uniform & bws_Exponential
    static int constexpr Range(const int i) { return ((limits[i].max - limits[i].min) + 1); }
    static int constexpr YalsStrategyCombinations() {
        auto result = 1;
        for (auto i = 0; i < StratCount; i++) { result *= Range(i);  }
        return result;
    }

    union {
        struct {
            CachedStrategy_t cached;
            StrategyProbSAT_t correct;
            InitStrategy_t InitStrategy; //pol
            int weight;
            BreakWeightStrategy_t BreakWeightStrategy;
        };
        int values[StratCount];
    };
    Strategy_t() { for (auto i = 0; const auto &l : limits) { values[i++] = l.def; } }
    void Randomize(RandomState_t& RandomState) {
        for (auto i = 0; const auto &l: limits) {
            const auto range = (l.max - l.min) + 1;
            assert1(range > 0);
            values[i++] = RandomState.NextInt(range) + l.min;
        }
    }
    Strategy_t MakeDefault() { *this = Strategy_t(); return *this; }
    std::string show() const {
        std::stringstream result;
        result << "cached:";
        switch (cached) {
            case cs_GetFromCache: result << "Y "; break;
            case cs_CreateNew:    result << "N "; break;
            default: assert1(false);
        }
        result << "PickVarHeur:";
        switch (correct) {
            case sps_UseProbSAT: result << "ProbSAT "; break;
            case sps_PowersOf2:  result << "Power2 "; break;
            default: assert1(false);
        }
        if (cs_CreateNew == cached) {
            result << "Init:";
            switch(InitStrategy) {
                case InitFalse:  result << "- "; break;
                case InitRandom: result << "? "; break;
                case InitTrue:   result << "+ "; break;
                case InitSmart:  result << "! "; break;
                default: assert1(false);
            }
        }
        result << "BreakWStrat = ";
        switch (BreakWeightStrategy) {
            case bws_Inverse:     result << "inv:"; break;
            case bws_Uniform:     result << "uni:"; break;
            case bws_Linear:      result << "lin:"; break;
            case bws_Exponential: result << "exp:"; break;
            default: assert1(false);
        }
        result << weight << " ";
        return result.str();
    }

};

//https://stackoverflow.com/a/49812018/650492
const std::string format(const char * const zcFormat, ...) {
    // initialize use of the variable argument array
    va_list vaArgs;
    va_start(vaArgs, zcFormat);

    // reliably acquire the size from a copy of the variable argument array
    // and a functionally reliable call to mock the formatting
    va_list vaArgsCopy;
    va_copy(vaArgsCopy, vaArgs);
    const int iLen = std::vsnprintf(nullptr, 0, zcFormat, vaArgsCopy);
    va_end(vaArgsCopy);

    if (iLen < 0) { return "error"; }

    // return a formatted string without risking memory mismanagement
    // and without assuming any compiler or platform specific behavior
    std::vector<char> zc(iLen + 1);
    std::vsnprintf(zc.data(), zc.size(), zcFormat, vaArgs);
    va_end(vaArgs);
    return std::string(zc.data(), iLen);
}

__forceinline__ constexpr int popcount64(const uint64_t data) {
    return std::popcount(data);
}

template <typename T>
uint32_t BestOutOfN(const T* Prefixsum, const uint32_t Length, const T RandomPoint) {
//Bottenbruch, Hermann "Structure and use of ALGOL 60". Journal of the ACM. 9 (2): p214. "Program for Binary Search".
    const auto max = Length;
    auto L = 0, M = 0;
    decltype(L) R = max;
    while (L != R) {
        M = ((L + R) + 1) / 2; //ciel
        if (Prefixsum[M] > RandomPoint) { R = M - 1; }
        else { L = M; }
    }
    //Correct for RandomPoint == PrefixSum[32], this can happen if the random is faulty and returns 0..n inclusive.
    const uint32_t result = (max == L)? L : L + 1;
#ifdef _DEBUG
    const auto Ist = result;
    for (auto Soll = 1; Soll <= max; Soll++) {
        //for robustness, assume random is 0 to max inclusive, WARNING: this test will not work in parallel code: do NOT port to GPU.
        if (max == Soll || (RandomPoint >= Prefixsum[Soll - 1] && RandomPoint < Prefixsum[Soll])) {
            assert2(Soll == Ist, "Binary search: Ist = %i, for loop: Soll = %i, Low:%f <= Random:%f <= High:%f", Ist, Soll, Prefixsum[Soll-1], RandomPoint, Prefixsum[Soll]);
            break;
        }
    }
#endif
    return result;
}


//Perform a binary search for the
template <typename T, unsigned long N>
uint32_t BestOutOfN(const std::array<T, N>& Prefixsum, const T RandomPoint) { //too bad we cannot use a span.
    const auto max = N - 1;
    return BestOutOfN(&Prefixsum[0], max, RandomPoint);
}

bool ScoreIsCloseEnough(const int64_t current_cost, const int64_t best_cost, const float scoremargin_percentage) {
    const auto diff = (current_cost - best_cost);
    if (diff < 0) { return true; }
    else {
        //-m --scoremargin cmd line parameters, 0.1% by default
        const auto OK_diff = best_cost * scoremargin_percentage;
        return (diff <= OK_diff);
    }
}



