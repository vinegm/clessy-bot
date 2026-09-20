#pragma once

#include "chess_types.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <vector>

class Position;

// NOTE: a mate is MATE_SCORE - ply, so shorter mates score higher
constexpr int MATE_SCORE = 32000;
constexpr int INFINITE_SCORE = MATE_SCORE + 1;
constexpr int MAX_SEARCH_DEPTH = 64;

constexpr bool is_mate_score(int score) {
  return score > MATE_SCORE - 1000 || score < -(MATE_SCORE - 1000);
}

/// @brief Distance to mate in moves, for a score is_mate_score accepted.
constexpr int mate_distance_moves(int score) {
  int plies = MATE_SCORE - std::abs(score);
  return (plies + 1) / 2;
}

// NOTE: negative means not set, times are in milliseconds
struct SearchLimits {
  int depth = MAX_SEARCH_DEPTH;
  int64_t nodes = -1;

  int64_t movetime = -1;
  int64_t wtime = -1;
  int64_t btime = -1;
  int64_t winc = 0;
  int64_t binc = 0;
  int movestogo = 0; // 0 is sudden death

  int mate = 0;
  bool infinite = false;
  std::vector<Move> searchmoves;
  int multipv = 1;

  int64_t move_overhead = 10;

  bool has_clock() const { return movetime >= 0 || wtime >= 0 || btime >= 0; }
};

/// @brief The handle the caller keeps on a search running on another thread.
///
/// Both flags are read on the search thread and written on the input thread,
/// which is the whole reason the UCI loop can answer "isready" and "stop"
/// while a search is in flight.
struct SearchControl {
  std::atomic<bool> stop{false};
  std::atomic<bool> pondering{false};
};

struct SearchResult {
  Move best_move{};
  int score = 0; // from the side to move's perspective
  int depth = 0;
  int seldepth = 0;
  int multipv = 1;

  uint64_t nodes = 0;
  int64_t elapsed_ms = 0;

  std::vector<Move> pv;
};

using SearchInfoCallback = std::function<void(const SearchResult &)>;

/**
 * @brief Iterative-deepening negamax alpha-beta search with quiescence.
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
