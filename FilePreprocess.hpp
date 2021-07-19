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
#include <stdio.h>
#include <chrono>
#include <iostream>
#include <fstream>
#include <string>
#include <cstdint>
#include <filesystem>
#include "ezOptionParser.hpp"
#include "Macros.hpp"


struct Parameters_t {
public:
    uint64_t MaxOuterRestarts = 1;
    int StartPhase1Flips = 1;
    int IncreasePhase1Flips = 1;
    int64_t LoopLengthPhase12 = 10'000;
    bool TrackVarDegree = false;
    bool AgeUnsatHard = true;
    int TopXItemsToPickFrom = 10;
    int64_t BaseFlipCount = 1000'000;
    int64_t MaxInnerRestarts = 1000'000;
    float scoremargin_percentage = 0.1f; //0,1%
    bool FirstDoRandomAssignment = false;
    int LogLevel = 2;
    int TimeOutInSecs = 45*60;//3600; //1 hour //120//2 mins
    int RandomSeed = 1;
    std::string Filename = "test.wcard";
    float hard_epsilon = 1.0f;//0.1f;
    float soft_epsilon = 1.0f;//0.1f;
    bool ExtraChecks = false;
    int RandomPickPercentage = 66;
private:
    mutable RandomState_t RandomState;
public:
    Strategy_t Strategy;
public:
    Parameters_t (): RandomState(1) {}
    void SetFilename(const std::string& NewFilename) { Filename = NewFilename; }
    std::string GetFilename() const { return Filename; }
    Parameters_t show() const { return *this; }
    void PickRandomStrategy() { Strategy.Randomize(RandomState); }
};


using namespace ez;

enum ParseResult_t { prSuccess, prErrorNoFilename, prWarningUnknownArgs };

/**
 * @brief Parse commandline options
 *
 * @param argc from main
 * @param argv from main
 * @param parameters everything is dumped here
 * @return int if (!= 0) quit with error code
 */
ParseResult_t ParseOptions(const int argc, const char* argv[], Parameters_t& parameters) {
    ParseResult_t result = prSuccess;
    ezOptionParser opt;
    opt.overview = "CarlSAT a stochastic local search SATlike solver using Boolean Cardinality Constraints";
    opt.syntax = "CarlSAT -f filename [options]";
    opt.example = "CarlSAT -z matrix1.wcard -n 1000000 -t 30 \n";
    opt.footer = "CarlSAT version 1.0 Copyright (C) 2021 Johan Bontes and Amazon.com, Inc, freely available under Apache 2.0 license\n" \
        "You can cite this work as \"J. Bontes, M. Heule, M. Whalen and J. Gain, CarlSAT: A Pure MaxSAT solver with cardinality support\"\n";

    opt.add("1",0, 1, 0, "Initial flipcount in phase I, default = 1", "-a", "--initialphase1");
    opt.add("1",0, 1, 0, "Increase in flipcount in phase I, default = 1", "-b", "--increasephase1");
    opt.add("10000",0,1,0,"Max flipcount in phase 1, default = 10000", "-c", "--maxflipphase1");
    opt.add("0", 0, 0, 0, "Enable extra debugging", "-d", "--debug");
    opt.add("0.1", 0, 1, 0, "Epsilon demoninators (hard), default \"0.1\"", "-e", "--hard_epsilon");
    opt.add("0.1", 0, 1, 0, "Epsilon demoninators (soft), default \"0.1\"", "-f", "--soft_epsilon");

    opt.add("", 0, 0, 0, "Display usage instructions", "-h", "--help");

    opt.add("66",0, 1, 0, "Random pick percentage for pickvar, default = 66", "-r", "--randompick");
    opt.add("1", 0, 1, 0, "Random seed 0 <= s <= UINT32_MAX, default = 1", "-s", "--randomseed");
    opt.add("0", 1, 1, 0, "Required: Timeout in seconds 0..INT32_MAX, default = 0 = forever", "-t", "--timeout");
    opt.add("10",0, 1, 0, "Top x items to consider to stochastic selection, default = 10", "-x", "--topx");
    opt.add("0", 0, 1, 0, "Log level verbosity 0..3, 0 = off, 3 = verbose \ndefault = 0", "-v", "--verbose");
    opt.add("", 1, 1, 0, "Required: Filename", "-z", "--file");

    auto print_instructions = [&](){
        std::string Instructions;
        opt.getUsage(Instructions);
        printf("%s", Instructions.c_str());
        exit(0);
    };

    if (nullptr == argv) { 
        print_instructions();    
    }

    opt.parse(argc, argv);
    if (opt.unknownArgs.size() > 0) {
        result = prWarningUnknownArgs;
        printf("Warning: unknown commandline arguments: ");
        for (auto i = 0; i < opt.unknownArgs.size(); i++) {
            printf("%s ", opt.unknownArgs[i]->c_str());
        }
        printf("\n These parameters will be ignored\n");
    }


    if (opt.isSet("-h")) {
        print_instructions();
    }

    parameters.StartPhase1Flips = 1;
    if (opt.isSet("-a")) {
        opt.get("-a")->getInt(parameters.StartPhase1Flips);
    }
    parameters.IncreasePhase1Flips = 1;
    if (opt.isSet("-b")) {
        opt.get("-b")->getInt(parameters.IncreasePhase1Flips);
    }
    parameters.LoopLengthPhase12 = 1000; //10'000;
    if (opt.isSet("-c")) {
        opt.get("-c")->getInt64(parameters.LoopLengthPhase12);
    }

    parameters.ExtraChecks = (opt.isSet("-d"));
    // parameters.TrackVarDegree = false;
    // if (opt.isSet("-d")) {
    //     opt.get("-d")->getBool(parameters.TrackVarDegree);
    // }

    //Factor to add to the denominator to prevent division by zero (adjusts the effect of small hard_break scores)
    parameters.hard_epsilon = 3.0f;
    if (opt.isSet("-e")) {
        opt.get("-e")->getFloat(parameters.hard_epsilon);
    }

    //Factor to add to the denominator to prevent division by zero (adjusts the effect of small soft_break scores)
    parameters.soft_epsilon = 3.0f;
    if (opt.isSet("-f")) {
        opt.get("-f")->getFloat(parameters.soft_epsilon);
    }

    parameters.RandomPickPercentage = 66;
    if (opt.isSet("-r")) {
        opt.get("-r")->getInt(parameters.RandomPickPercentage);
        parameters.RandomPickPercentage = abs(parameters.RandomPickPercentage); 
    }

    parameters.TopXItemsToPickFrom = 10;
    if (opt.isSet("-x")) {
        opt.get("-x")->getInt(parameters.TopXItemsToPickFrom);
    }

    // parameters.FirstDoRandomAssignment = false;
    // if (opt.isSet("-r")) {
    //     parameters.FirstDoRandomAssignment = true;
    // }

    int TimeoutInSecs = 0;
    if (opt.isSet("-t")) {
        opt.get("-t")->getInt(TimeoutInSecs);
    } else {
        const auto str_timeout = "TIMELIMIT";
        auto timeout_enviroment_str = getenv(str_timeout);
        if (nullptr != timeout_enviroment_str) {
            if (1 != sscanf(timeout_enviroment_str, "%i", &TimeoutInSecs)) { TimeoutInSecs = 0; }
        } else {
            const auto str_timeout = "SATTIMEOUT";
            auto timeout_enviroment_str = getenv(str_timeout);
            if (nullptr != timeout_enviroment_str) {
                if (1 != sscanf(timeout_enviroment_str, "%i", &TimeoutInSecs)) { TimeoutInSecs = 0; }
            }
        }
    }
    parameters.TimeOutInSecs = TimeoutInSecs;

    parameters.RandomSeed = 1;
    if (opt.isSet("-s")) {
        opt.get("-s")->getInt(parameters.RandomSeed);
    }
    srand(parameters.RandomSeed); //set the randomseed globally. rand is used (only) to initialize our own random generator

    parameters.LogLevel = 1;
    if (opt.isSet("-v")) {
        int loglevel;
        opt.get("-v")->getInt(loglevel);
        if (loglevel < 0) { loglevel = 0; }
        if (loglevel > 3) { loglevel = 3; }
        parameters.LogLevel = loglevel;
    }

    if (opt.isSet("-z")) {
        std::string filename;
        opt.get("-z")->getString(filename);
        parameters.SetFilename(filename);
    } else {
        printf("Error: No filename given, terminating now because I have no data to process");
        return prErrorNoFilename;
    }

    // parameters.MaxInnerRestarts = 1'000'000;
    // if (opt.isSet("-i")) {
    //     opt.get("-i")->getInt64(parameters.MaxInnerRestarts);
    // }

    // parameters.MaxOuterRestarts = 1;
    // if (opt.isSet("-o")) {
    //     opt.get("-o")->getUInt64(parameters.MaxOuterRestarts);
    // }

    // parameters.BaseFlipCount = 100'000;
    // if (opt.isSet("-n")) {
    //     opt.get("-n")->getInt64(parameters.BaseFlipCount);
    // }

    printf("c Parameters: ");
    for (auto i = 0; i < argc; i++) {
        printf("%s ", argv[i]);
    }
    printf("\n");
    const auto p = parameters;
    printf( "c StartPhase1Flips = %i, IncreasePhase1Flips = %i, LoopLengthPhase12 = " _lli_ "\n" \
            "c Track degree of vars = %i, Age unsat hard clauses = %i, Pick from top %i items \n" \
            "c Loglevel = %i, Timeout = %i secs, \n"
            "c MaxOuterRestarts = " _llu_ ", BaseFlipCount = " _lli_ ", MaxInnerRestarts = " _llu_ ", FirstDoRandomAssignment = %i\n" \
            "c RandomSeed = %u \n" \
            "c Filename = %s\n",
    p.StartPhase1Flips, p.IncreasePhase1Flips, p.LoopLengthPhase12,
    p.TrackVarDegree, p.AgeUnsatHard, p.TopXItemsToPickFrom,
    p.LogLevel, p.TimeOutInSecs,
    p.MaxOuterRestarts,      p.BaseFlipCount,      p.MaxInnerRestarts,      p.FirstDoRandomAssignment,
    p.RandomSeed,
    p.GetFilename().c_str() );
    return result;
}

std::string GetExtension(const std::string& filename) {
    const auto path = std::filesystem::path(filename);
    return path.stem();
}

enum filetype_t { ft_wcard, ft_wcnf };

filetype_t GetFileType(const std::string& filename) {
    auto ext = GetExtension(filename);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if      ("wcard" == ext) { return ft_wcard; }
    else if ("wcnf" == ext)  { return ft_wcnf; }
    else assert(false);
    return ft_wcard;
}


