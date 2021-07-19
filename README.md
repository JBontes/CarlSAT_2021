# CarlSAT

### CarlSAT: A Scalable MaxSAT Solver for Integer Optimization Problems

CarlSAT is developed by Johan Bontes (UCT), Marijn Heule (CMU/Amazon) and Mike Whalen (Amazon) as part of Johan's applied scientist internship at the automated reasoning group.

## About

Optimization problems with complex constraints occur in many contexts, including scheduling / planning, vehicle routing, bin packing, and placement problems. One promising technique for representing and solving integer optimization problems with Boolean constraints is called partial MaxSAT, which is a generalization of Boolean Satisfiability problem to include clauses that must be satisfied ('hard' clauses) and clauses that are not required to be satisfied but have a cost for non-satisifaction ('soft' clauses).


We introduce CarlSAT, a novel stochastic local search solver for a subset of partial MaxSAT problems that can be stated in 'pure' form. In pure problems all literals in hard (required) clauses have a single polarity opposite from that of literals in soft (optional) clauses; this simplifies the structure of the problem, allowing for more efficient processing. The solver features native support for boolean cardinality constraints, which occur commonly in MaxSAT problems. This negates the need to encode these constraints into conjunctive normal form, thereby avoiding the geometric expansion in problem size associated with this translation. We introduce a new 'wcard' file format supporting cardinality clauses. Processing cardinality clauses adds a small overhead whereas the smaller problem size makes for much larger savings. Finally, CarlSAT eliminates the clause selection step traditionally used in local search SAT solvers. These innovations result in a solver that outperforms the current state-of-art in local search MaxSAT solving. It can be applied to a large subset of NP-Complete problems, such as set cover and bin packing.

## Usage

    ./carlsat --help

Gives a full list of commandline arguments.

After compilation, CarlSAT can be run from the commandline using the command:

    carlsat -z filename -t timeout [options]

Both `-z` and `-t` are required, all other arguments are optional.

### Input files

CarlSAT accepts input files in `.wcnf` or `.wcard` file format.
`.wcnf` files should adhere to the wdimacs format described in http://www.maxhs.org/docs/wdimacs.html.
`.wcard` files add support for boolean cardinality constraints to `.wcnf`, and are explained in the **wcard file format** section below.

CarlSAT assumes all input files are in 'pure' form. This means that all literals in all soft clause are either
all positive or all negative, but not a mix of the two. All literals in all hard clauses are the opposite
polarity of soft clauses, meaning that there are no literals with mixed polarity in the hard clauses as well.

### Timeout

The timeout is specified as a whole number of seconds, e.g.: `-t 60` for a 1 minute timeout.
The solver may take slightly longer than the timeout to finish, but never shorter.

### Optional commandline arguments

In order to understand the optional arguments an overview of the core algorithm is useful.
This is given in the **algorithm** section.

    -a, --initialphase1
The initial number of flips in phase I.

    -b, --increasephase1

The increase in flip count after -c loops have completed

    -c, --maxflipphase1

The number of loops to run both Phase I and II until -a is increased.
This parameter is subject to a reluctant doubling (Luby) sequence.

    -d, --debug

Run the solver with assertions enabled.

    -e, --hard_epsilon
    -f, --soft_epsilon

Variables are picked for flipping using a following formula confronting the make score (the number of clause satisfied when the variable flips) with its break score (the number of clauses unsatisfied by the flip). Soft make/break scores take the weight of the clause into account.

    soft_var = soft_make / (hard_break + soft_epsilon)
    hard_var = hard_make / (soft_break + hard_epsilon)

The high hard and soft epsilons moderate the effect of high hard break values,
low epsilons emphasises the effect of break values.

    -h, --help

Display a list of available command line arguments.

    -r, --randompick

A number between 0 and 100, the percentage of the time a random variable pick is performed instead of a stochastic pick.
r = 0: variables are always stochastically picked based on the above formula, r = 100 variables are always picked at random.

    -x, --topx

The top *x* number of best scoring variables to take into account in the stochastic pick.
1 = the best variable is always selected in stochastic pick; *x* > 1 = a stochastic pick is performed taking the top *x* scoring variables into account.

    -s, --randomseed

The random seed used to initalize the random generator.

    -t, --timeout

Required: the minimum number of seconds the solver will spend on the problem. The solver may go over this limit by several milliseconds.

    -v, --verbose

The verbosity of reporting:

     0 = Only report the final score
     1 = default: report improvements in solve score every time the solver gets stuck (100s to 1000s of lines)
     2 = report every individual improvement in score (possibly millions of lines)
     3 = report every step the solver takes (you asked for it...)

## Algorithm

    p = commandline parameters
    for (;;) {
        a = p.a;
        c = p.c;
        for (loop = 0; loop < c; loop++) {
            //Phase I: Optimize the score on soft clauses
            for (i = 0; i < a; i++) {
                v = pick an unsat soft variable using (p.e, p.r, p.s)
                flip(v)
                update_score()
            }
            //Phase II: Repair hard clauses
            while (score < best_score) {
                v = pick an unsat hard variable using (p.f, p.r, p.s)
                flip(v)
                update_score
            }
            if (unsat_hard_count = 0) {
                best_score = score;
                a = p.a;
            }
        }
        a += p.b; //increase exploration
        c = p.c * LubySequence.Next()
    }

## Compiling the source

Clang-11 is required. This is installed by default on OSX >= 10.15 and can be installed on ubuntu using

    sudo apt install clang-11 lldb-11 lld-11 make

building the source:

    cd src && make clean && make

## Source files

The source consists of the following files:

```
PureCard.cpp       - The **main** file
Macros.hpp         - Essential include in every file, provides assertions and logging
DataTypes.hpp      - All the int sized primitive types used in the SAT solver, it's full of bitfields
ezOptionParser.hpp - Remik Ziemlinski's command argument parser
FilePreprocess.hpp - Parses commandline arguments (and perhaps later file preprocessing)
HashTable          - A linear probe hashtable and solution store
murmurhash3.*      - Austin Appleby's original code. I use a simplified version. There to make sure I haven't messed up.

## `.wcard` file format

The `.wcard` fileformat is an extension of `.wcnf` that supports boolean cardinality constraints natively.

the header is thesame as a wcnf header except that 'wcnf' is replaced by 'wcard':

    p wcard <var_count> <clause_count> <magic_hard_clause_number>

Soft clauses can have **any** weight, except for the magic_hard_clause_number.
Fixed costs can be denoted by an empty soft clause; e.g.: a fixed cost of 2347664 is marked as follows

    2347664 0

Only hard clauses can be cardinality constraints and look as follows, (assuming magic_hard_clause_weight = 1000).

    p wcard 6 4 1000

    c 'at_least' clauses
    1000 1 2 3 4 5 6 <= 2
    1000 1 2 3 4 5 6 < 3

    c equivalent 'at_most' clauses
    1000 -1 -2 -3 -4 -5 -6 >= 4
    1000 -1 -2 -3 -4 -5 -6 > 3

Note that all the above clauses are equivalent.
Exactly equal_to and not_equal_to contraints are also allowed.

    p wcard 6 2 1000

    1000 1 2 3 4 5 6 = 3
    1000 -1 -2 -3 -4 -5 -6 != 3

Note that these clauses are not equivalent.

Although the `.wcard` file format does not require this, CarlSAT can only process 'pure' problems.

The folloing problem is pure:

    p wcard 3 5 1000
    10 1 2 3 0           //all positive literals in soft clause
    124222 0             //empty soft clause denoting fixed cost
    1000 -1 -2 -3 0      //boolean hard clause with all negative literals
    1000 -1 -2 -3 >= 2   //at_least hard clause with negative literals.
    1000 1 2 3 <= 1      //al_most hard clause equivalent to the previous line.

CarlSAT cannot handle equality and non_equality cardinality clauses, because these cannot
be represented in pure form when translated into at_least clauses.
(This limitation may be lifted in future versions).

## Performance

The ARG team has benchmarked CarlSAT against leading ILP+ solvers (X-press, CPlex, and Gurobi) using a number of
real-world middle mile problems (test results available on request, contact mww@amazon.com).
Version 0.1 outperforms all of them on their default settings when processing the data in wcard form and comes
within 1-2% of the best performance by the ILP+ solvers.

CarlSAT currently does not use preprocessing; we expect performance to improve once preprocessing is added.
Because this solver processes cardinality constraints in their native form inside the solver, we cannot use
existing preprocessors for wcard files.

For pure wcnf files, CarlSAT outperforms the award winning Loandra MaxSAT solver by a wide margin on a large number of benchmarks.

Any pure wcnf files that can be expressed using only unit clauses for the soft constraints is more efficiently solved using the
LinearLS solver, contact Mike Whalen (mww@amazon.com) to obtain a version of this solver.

## Availability

The source code for CarlSAT is available from: .....

## Todo items

- restarts need to be implemented properly
- integrate reading equations on the commandline (should work if you make sure there are no spaces)
- improve the heuristics

## References

LinearLS: .....
Loundra: .....

## Link to test set used:

........


