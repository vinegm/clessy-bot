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
    // NOTE: only the top 16 bits, the low ones already picked the cluster
    uint16_t key_signature = 0;
    uint16_t packed_move = 0;
    int16_t score = 0;
    int8_t depth = -1;
    uint8_t generation_bound = 0; // [generation:6][bound:2]

    Move move() const { return Move::from_raw(packed_move); }
    Bound bound() const { return Bound(generation_bound & BOUND_MASK); }
  };

  static_assert(sizeof(Entry) == 8, "a transposition table entry must stay 8 bytes");

  static constexpr size_t DEFAULT_MB = 64;
  static constexpr size_t CLUSTER_SIZE = 4;

  static void resize(size_t mb);
  static void clear();
  static void new_search();
  static size_t hashfull();
  static const Entry *probe(uint64_t key);

  // NOTE: mate scores must be relative to the node, not the root
  static void store(uint64_t key, const Move &move, int score, int depth, Bound bound);

private:
  // NOTE: one cache line, probed and replaced as a unit
  struct alignas(32) Cluster {
    Entry entries[CLUSTER_SIZE];
  };

  static constexpr uint8_t BOUND_MASK = 0x3;
  static constexpr uint8_t GENERATION_STEP = 0x4;
  static constexpr uint8_t GENERATION_MASK = 0xFC;
  static constexpr int GENERATION_CYCLE = 0x100;

  static inline std::vector<Cluster> table;
  static inline uint64_t mask = 0;
  static inline uint8_t generation = 0;

  static constexpr uint16_t key_signature_of(uint64_t key) {
    return static_cast<uint16_t>(key >> 48);
  }
};
