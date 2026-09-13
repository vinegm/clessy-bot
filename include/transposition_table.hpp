#pragma once

#include "chess_types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

class TranspositionTable {
public:
  TranspositionTable() = delete;

  enum Bound : uint8_t {
    NONE,
    EXACT,
    LOWER,
    UPPER
  };

  struct Entry {
    uint64_t key = 0;
    Move move{};
    int16_t score = 0;
    int8_t depth = -1;
    Bound bound = NONE;
  };

  static constexpr size_t DEFAULT_MB = 64;

  static void resize(size_t mb);
  static void clear();
  static size_t hashfull();
  static const Entry *probe(uint64_t key);

  // NOTE: mate scores must be relative to the node, not the root
  static void store(uint64_t key, const Move &move, int score, int depth, Bound bound);

private:
  static inline std::vector<Entry> table;
  static inline uint64_t mask = 0;
};
