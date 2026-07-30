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

  /**
   * One stored search result, packed into 8 bytes.
   *
   * The key is truncated to its top 16 bits because the low bits already
   * picked the cluster, so storing them again proves nothing. A collision then
   * costs a wrong ordering hint or a wrong cutoff about once in 65536 probes
   * of the same cluster, which is the standard trade for fitting four entries
   * on one cache line.
   *
   * Generation and bound share a byte: bound in bits 0-1, generation in bits
   * 2-7. The generation is what lets replacement prefer a shallow entry from
   * this search over a deeper one left behind several moves ago, which a
   * depth-only policy cannot tell apart.
   */
  struct Entry {
    uint16_t key16 = 0;
    uint16_t move16 = 0; // best or refutation move, used for ordering
    int16_t score = 0;
    int8_t depth = -1;
    uint8_t gen_bound = 0;

    Move move() const { return Move::from_raw(move16); }
    Bound bound() const { return Bound(gen_bound & BOUND_MASK); }
  };

  static_assert(sizeof(Entry) == 8, "TT entry must stay 8 bytes");

  static constexpr size_t DEFAULT_MB = 64;

  // Entries sharing one cluster, and so one cache line: a probe reads all four
  // for the price of the first.
  static constexpr size_t CLUSTER_SIZE = 4;

  // Allocate the table with the largest power-of-two cluster count
  // fitting in the given size. Clears all entries.
  static void resize(size_t mb);
  static void clear();

  // Advance the generation. Called once per search, so entries left behind by
  // earlier moves become the preferred replacement victims.
  static void new_search();

  // How full the table is, in permille, for UCI "info hashfull".
  // NOTE: This is a rough estimate, not an exact count. It samples the first entries rather
  // than counting all of them, since the answer is reported once per iteration and only ever
  // displayed. Only entries from the current generation count as used.
  static size_t hashfull();

  // Look up a position.
  static const Entry *probe(uint64_t key);

  /**
   * Store a search result.
   *
   * Prefers the entry already holding this position, then an empty slot, then
   * the cluster's least valuable entry, valuing depth against age so a deep
   * entry from a finished search still loses to a useful current one.
   */
  static void store(uint64_t key, const Move &move, int score, int depth, Bound bound);

private:
  static constexpr uint8_t BOUND_MASK = 0x3;
  static constexpr uint8_t GEN_DELTA = 0x4; // one generation step within gen_bound
  static constexpr uint8_t GEN_MASK = 0xFC; // the bits gen_bound lends the generation
  static constexpr int GEN_CYCLE = 0x100;   // gen_bound wraps at a byte
};
