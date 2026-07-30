#pragma once

#include "chess_types.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

class Position;

// Mate scores are encoded as MATE_SCORE - ply, so shorter mates score higher.
constexpr int MATE_SCORE = 32000;
constexpr int INF_SCORE = MATE_SCORE + 1;
constexpr int MAX_SEARCH_DEPTH = 64;

constexpr bool is_mate_score(int score) {
  return score > MATE_SCORE - 1000 || score < -(MATE_SCORE - 1000);
}

// Distance to mate in moves, for a score is_mate_score accepted.
constexpr int mate_distance_moves(int score) {
  int plies = MATE_SCORE - (score > 0 ? score : -score);
  return (plies + 1) / 2;
}

struct SearchLimits {
  int depth = MAX_SEARCH_DEPTH;

  // Negative means "not set". A node budget is machine-independent, so it is
  // the limit to prefer when positions must be searched to equal effort.
  int64_t nodes = -1;

  // Milliseconds; negative means "not set".
  int64_t movetime = -1;
  int64_t wtime = -1;
  int64_t btime = -1;
  int64_t winc = 0;
  int64_t binc = 0;

  // Moves left in the current time control period; 0 means sudden death.
  int movestogo = 0;

  // "go mate x": look for a mate in x moves. 0 means not asked for.
  int mate = 0;

  // "go infinite": search until stopped, whatever the clock says.
  bool infinite = false;

  // Root moves the GUI restricted the search to. Empty means all of them.
  std::vector<Move> searchmoves;

  // Lines to report. Above 1 the search costs roughly one extra root pass per
  // line, so this is a UCI convenience rather than a strength feature.
  int multipv = 1;

  // Milliseconds held back from every time budget to cover the delay between
  // the engine deciding and the GUI receiving it.
  int64_t move_overhead = 10;

  bool has_clock() const { return movetime >= 0 || wtime >= 0 || btime >= 0; }
};

// The handle the caller keeps on a search running on another thread.
//
// Both flags are read on the search thread and written on the input thread,
// which is the whole reason the UCI loop can answer "isready" and "stop"
// while a search is in flight.
struct SearchControl {
  // Set by "stop" and "quit". Honoured everywhere, including at depth 1 where
  // the time and node budgets are deliberately ignored.
  std::atomic<bool> stop{false};

  // True while this is a ponder search. Budgets do not apply until "ponderhit"
  // clears it, because that is the moment the engine's clock actually starts.
  std::atomic<bool> pondering{false};
};

struct SearchResult {
  Move best_move{};
  int score = 0; // centipawns (or mate score) from the side to move
  int depth = 0;

  // Deepest ply reached anywhere, quiescence included. Always at least depth.
  int seldepth = 0;

  // 1-based index of this line among the MultiPV lines; 1 is the best line.
  int multipv = 1;

  uint64_t nodes = 0;
  int64_t elapsed_ms = 0;

  // The line the search expects to be played, best_move first. Ends where the
  // main search does; quiescence does not extend it, and a node that returned
  // on a table cutoff contributes nothing, so it can be shorter than depth.
  std::vector<Move> pv;
};

// Called once per reported line after each completed iteration of iterative
// deepening. With MultiPV above 1 it fires once per line, best line first.
using SearchInfoCallback = std::function<void(const SearchResult &)>;

/**
 * Iterative-deepening negamax alpha-beta search with quiescence.
 *
 * Deepens until limits.depth, the node budget, the time budget, or control's
 * stop flag. The time budget comes from movetime when set, otherwise from the
 * side to move's clock (a slice sized by movestogo, plus half the increment,
 * less the move overhead). A ponder search arms no deadline until control's
 * pondering flag clears. The position is restored before returning.
 *
 * @param pos The position to search
 * @param limits Depth, clock and root-move limits
 * @param on_iteration Optional callback fired for each reported line
 * @param control Optional stop/ponder handle owned by the caller
 * @return Best line of the last completed iteration
 */
SearchResult run_search(
    Position &pos,
    const SearchLimits &limits,
    const SearchInfoCallback &on_iteration = nullptr,
    SearchControl *control = nullptr
);
