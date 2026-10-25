#pragma once

#include "chess_types.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <vector>

class Position;

struct SearchLimits;
struct SearchResult;
struct SearchControl;

using SearchInfoCallback = std::function<void(const SearchResult &)>;

class Search {
public:
  Search() = delete;

  // NOTE: a mate is MATE_SCORE - ply, so shorter mates score higher
  static constexpr int MATE_SCORE = 32000;
  static constexpr int INFINITE_SCORE = MATE_SCORE + 1;
  static constexpr int MAX_DEPTH = 64;

  static SearchResult
      run(Position &pos,
          const SearchLimits &limits,
          const SearchInfoCallback &on_iteration = nullptr,
          SearchControl *control = nullptr);

  // NOTE: forgets what move ordering learned, for a new game
  static void clear();

  static constexpr bool is_mate_score(int score) {
    return score > MATE_SCORE - 1000 || score < -(MATE_SCORE - 1000);
  }

  static constexpr int mate_distance_moves(int score) {
    int plies = MATE_SCORE - std::abs(score);
    return (plies + 1) / 2;
  }
};

// NOTE: negative means not set, times are in milliseconds
struct SearchLimits {
  int depth = Search::MAX_DEPTH;
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

// NOTE: written by the input thread while the search thread reads it
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
