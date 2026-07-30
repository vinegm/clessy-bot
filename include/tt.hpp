#pragma once

#include "chess_types.hpp"

#include <cstddef>
#include <cstdint>

// singleton transposition table for the search
class TranspositionTable {
public:
  TranspositionTable() = delete;

  // What the stored score proves about the node it came from.
  enum Bound : uint8_t {
    NONE,
    EXACT, // score is the exact search value
    LOWER, // score is a lower bound (fail-high / beta cutoff)
    UPPER  // score is an upper bound (fail-low)
  };

  struct Entry {
    uint64_t key = 0;
    Move move{}; // best or refutation move, used for ordering
    int16_t score = 0;
    int8_t depth = -1;
    Bound bound = NONE;
  };

  static constexpr size_t DEFAULT_MB = 64;

  // Allocate the table with the largest power-of-two entry count
  // fitting in the given size. Clears all entries.
  static void resize(size_t mb);
  static void clear();

  // How full the table is, in permille, for UCI "info hashfull".
  // NOTE: This is a rough estimate, not an exact count. It samples the first entries rather
  // than counting all of them, since the answer is reported once per iteration and only ever
  // displayed.
  static size_t hashfull();

  // Look up a position.
  static const Entry *probe(uint64_t key);

  // Store a search result. Keeps the existing entry when
  // it holds a deeper search of the same position.
  static void store(uint64_t key, const Move &move, int score, int depth, Bound bound);
};
