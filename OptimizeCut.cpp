/******************************************************************************
 *
 * Load Value Injection (LVI) Minimum Multi-Cut Optimization Plugin for LLVM
 *
 * Copyright (c) 2020, Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 ******************************************************************************/

#include <algorithm>
#include <cassert>
#include <coin/symphony.h>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <vector>

#define GADGET_EDGE ((int)(-1))
#define WEIGHT(EdgeValue) ((double)(2 * (EdgeValue) + 1))
#ifndef NDEBUG
#define DEBUG(x)                                                               \
  do {                                                                         \
    x;                                                                         \
  } while (false)
#else
#define DEBUG(x)
#endif
#define FATAL_ERROR(msg)                                                       \
  do {                                                                         \
    std::cerr << (msg) << '\n';                                                \
    exit(EXIT_FAILURE);                                                        \
  } while (false)

static void *_malloc(size_t size) {
  void *mem = malloc(size);
  if (mem == nullptr)
    FATAL_ERROR("malloc() failed");
  return mem;
}

static void *_calloc(size_t num, size_t size) {
  void *mem = calloc(num, size);
  if (mem == nullptr)
    FATAL_ERROR("malloc() failed");
  return mem;
}

extern "C" int optimize_cut(unsigned int *nodes, unsigned int nodes_size,
                            unsigned int *edges, int *edge_values,
                            int *cut_edges /* out */, unsigned int edges_size) {
  using NodeSet = std::vector<bool>;
  using EdgeSet = std::vector<bool>;
  std::fill(cut_edges, cut_edges + edges_size, false);
  int PathsFound = 0, NonZeros = 0, Gadgets = 0;
  auto *Paths = new std::vector<int>[edges_size];
  NodeSet Visited(nodes_size, false), GadgetSinks(nodes_size, false);
  EdgeSet Path(edges_size, false);
  for (auto *NI = nodes, *const NE = nodes + nodes_size; NI != NE; ++NI) {
    // collect the gadgets for this node
    for (auto *CEI = edges + *NI, *const CEE = edges + *(NI + 1); CEI != CEE;
         ++CEI) {
      if (edge_values[CEI - edges] == GADGET_EDGE) {
        GadgetSinks[*CEI] = true;
        ++Gadgets;
      }
    }
    if (Gadgets == 0)
      continue;

    // Find one path per gadget using DFS
    std::function<bool(unsigned int *, bool)> FindOnePathPerGadget =
        [&](unsigned int *N, bool FirstNode) {
          if (!FirstNode) {
            Visited[N - nodes] = true;
            if (GadgetSinks[N - nodes]) {
              for (unsigned int I = 0; I < edges_size; ++I) {
                if (Path[I]) {
                  Paths[I].push_back(PathsFound);
                  ++NonZeros;
                }
              }
              ++PathsFound;
              GadgetSinks[N - nodes] = false;
              if (--Gadgets == 0)
                return true; // found all the gadgets originating at `NI`
            }
          }
          for (auto *CEI = edges + *N, *const CEE = edges + *(N + 1);
               CEI != CEE; ++CEI) {
            if (edge_values[CEI - edges] != GADGET_EDGE && !Visited[*CEI]) {
              Path[CEI - edges] = true;
              if (FindOnePathPerGadget(nodes + *CEI, false))
                return true;
              Path[CEI - edges] = false;
            }
          }
          return false;
        };
    __attribute__((unused)) bool FoundAllPaths = FindOnePathPerGadget(NI, true);
    assert(FoundAllPaths);
    Path.assign(edges_size, false);
    Visited.assign(nodes_size, false);
  }
  DEBUG(std::cerr << "Found " << PathsFound << " paths"
                  << " with " << NonZeros << " total non-zero constraints\n");

  // Determine which edges are part of some path
  std::vector<int> NonEmptyCols;
  for (unsigned int EdgeI = 0; EdgeI < edges_size; ++EdgeI)
    if (!Paths[EdgeI].empty())
      NonEmptyCols.push_back(EdgeI);

  if (NonEmptyCols.size() == 1) { // trivial solution
    cut_edges[NonEmptyCols[0]] = true;
    delete[] Paths;
    return 0;
  }

  // Construct the problem matrix using column-major form
  int NumRows = PathsFound, NumCols = NonEmptyCols.size();
  assert(NumRows > 0 && NumCols > 0);
  int I = 0, *RowIdx = (int *)_malloc(sizeof(int) * NonZeros),
      *ColStart = (int *)_malloc(sizeof(int) * (NumCols + 1));
  for (int ColI = 0; ColI < NumCols; ++ColI) {
    ColStart[ColI] = I;
    for (int &RowI : Paths[NonEmptyCols[ColI]]) {
      RowIdx[I++] = RowI;
    }
  }
  delete[] Paths;
  assert(I == NonZeros);
  ColStart[NumCols] = I;
  double *Values = (double *)_malloc(sizeof(double) * NonZeros);
  std::uninitialized_fill_n(Values, NonZeros, 1.0);
  double *ColLB = (double *)_calloc(NumCols, sizeof(double));
  double *ColUB = (double *)_malloc(sizeof(double) * NumCols);
  std::uninitialized_fill_n(ColUB, NumCols, 1.0); // column UB is 1
  char *IsInt = (char *)_malloc(sizeof(char) * NumCols);
  std::uninitialized_fill_n(IsInt, NumCols, TRUE);
  double *ObjFunction = (double *)_malloc(sizeof(double) * NumCols);
  for (int ColI = 0; ColI < NumCols; ++ColI) {
    ObjFunction[ColI] = WEIGHT(edge_values[NonEmptyCols[ColI]]);
  }
  char *RowConstraints = (char *)_malloc(sizeof(char) * NumRows);
  std::uninitialized_fill_n(RowConstraints, NumRows, 'G'); // ">=" constraint
  double *RowRHS = (double *)_malloc(sizeof(double) * NumRows);
  std::uninitialized_fill_n(RowRHS, NumRows, 1.0); // row RHS is 1 for all rows

  // load the problem into Symphony, and solve
  sym_environment *Symphony = sym_open_environment();
  if (!Symphony)
    FATAL_ERROR("Failed to initialize the Symphony MILP solver");
  if (sym_explicit_load_problem(
          Symphony, NumCols, NumRows, ColStart, RowIdx, Values, ColLB, ColUB,
          IsInt, ObjFunction, nullptr /* ObjFunction2 - not used */,
          RowConstraints, RowRHS, nullptr /* row ranges - not used */,
          false /* Do not make copies of the input arrays */) !=
      FUNCTION_TERMINATED_NORMALLY) {
    FATAL_ERROR("Failed to load MILP matrix into Symphony");
  }
#ifndef NDEBUG
  sym_set_int_param(Symphony, "verbosity", 0);
#else
  sym_set_int_param(Symphony, "verbosity", -2);
#endif
  int SymphonyResult = sym_solve(Symphony);
  if (SymphonyResult != TM_OPTIMAL_SOLUTION_FOUND) {
    DEBUG({
      std::cerr << "Failed to solve MILP matrix with Symphony: ";
      const char *FailureReason;
      switch (SymphonyResult) {
      case ERROR__USER:
        FailureReason = "Symphony internal error";
        break;
      case TM_TIME_LIMIT_EXCEEDED:
        FailureReason = "TM stopped after reaching the predefined time limit.";
        break;
      case TM_NODE_LIMIT_EXCEEDED:
        FailureReason = "TM stopped after reaching the predefined node limit.";
        break;
      case TM_TARGET_GAP_ACHIEVED:
        FailureReason = "TM stopped after achieving the predefined target gap.";
        break;
      case TM_FOUND_FIRST_FEASIBLE:
        FailureReason = "TM stopped after finding the first feasible solution.";
        break;
      case TM_ERROR__NO_BRANCHING_CANDIDATE:
        FailureReason = "Error. TM stopped. User didn’t select branching "
                        "candidate in user_select_candidates() callback.";
        break;
      case TM_ERROR__ILLEGAL_RETURN_CODE:
        FailureReason =
            "Error. TM stopped after getting a non-valid return code.";
        break;
      case TM_ERROR__NUMERICAL_INSTABILITY:
        FailureReason = "Error. TM stopped due to some numerical difficulties.";
        break;
      case TM_ERROR__COMM_ERROR:
        FailureReason = "Error. TM stopped due to communication error.";
        break;
      case TM_ERROR__USER:
        FailureReason = "Error. TM stopped. User error detected in one of user "
                        "callbacks called during TM processes";
        break;
      default:
        FATAL_ERROR("Invalid Symphony error");
      }
      std::cerr << FailureReason << std::endl;
    });
    if (SymphonyResult == TM_TIME_LIMIT_EXCEEDED ||
        SymphonyResult == TM_NODE_LIMIT_EXCEEDED ||
        SymphonyResult == TM_TARGET_GAP_ACHIEVED ||
        SymphonyResult == TM_FOUND_FIRST_FEASIBLE) {
      // not a hard error; we can recover by falling back to naive heuristic
      sym_close_environment(Symphony);
      return -1;
    } else {
      FATAL_ERROR("Fatal Symphony error");
    }
  }
  double *Solution = new double[NumCols];
  if (sym_get_col_solution(Symphony, Solution) != FUNCTION_TERMINATED_NORMALLY)
    FATAL_ERROR("Failed to retrieve column solution from Symphony");
  int NumEdgesCut = 0;
  for (int ColI = 0; ColI < NumCols; ++ColI) {
    if (Solution[ColI] > 0.0) {
      cut_edges[NonEmptyCols[ColI]] = true;
      ++NumEdgesCut;
    }
  }
  DEBUG(std::cerr << "Cut " << NumEdgesCut << " edges\n");
  delete[] Solution;
  sym_close_environment(Symphony);
  return 0;
}
