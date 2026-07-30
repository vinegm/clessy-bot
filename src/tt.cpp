#include "tt.hpp"

#include <vector>

namespace {
using Entry = TranspositionTable::Entry;

// One cache line's worth of entries, probed and replaced as a unit.
struct alignas(32) Cluster {
  Entry entries[TranspositionTable::CLUSTER_SIZE];
};

// Singleton transposition table, since the TT is
// global and mutable after resize.
std::vector<Cluster> table;
uint64_t mask = 0;

// Advanced once per search by new_search, in GEN_DELTA steps so the low two
// bits stay free for the bound.
uint8_t generation = 0;

// The top bits of the key, the ones the cluster index did not already consume.
constexpr uint16_t key16_of(uint64_t key) { return static_cast<uint16_t>(key >> 48); }
} // namespace

void TranspositionTable::resize(size_t mb) {
  size_t max_clusters = mb * 1024 * 1024 / sizeof(Cluster);

  size_t clusters = 1;
  while (clusters * 2 <= max_clusters) {
    clusters *= 2;
  }

  table.assign(clusters, Cluster{});
  mask = clusters - 1;
}

void TranspositionTable::clear() { table.assign(table.size(), Cluster{}); }

void TranspositionTable::new_search() { generation += GEN_DELTA; }

size_t TranspositionTable::hashfull() {
  if (table.empty()) return 0;

  size_t sampled = 0;
  size_t used = 0;
  for (size_t i = 0; i < table.size() && sampled < 1000; i++) {
    for (const Entry &entry : table[i].entries) {
      if (sampled++ >= 1000) break;

      // Entries left by finished searches are replaceable, so they do not
      // count against how full this search's table is.
      if (entry.bound() != NONE && (entry.gen_bound & GEN_MASK) == generation) used++;
    }
  }

  return sampled ? used * 1000 / sampled : 0;
}

const TranspositionTable::Entry *TranspositionTable::probe(uint64_t key) {
  if (table.empty()) return nullptr;

  const Cluster &cluster = table[key & mask];
  const uint16_t wanted = key16_of(key);

  for (const Entry &entry : cluster.entries) {
    if (entry.key16 == wanted && entry.bound() != NONE) return &entry;
  }

  return nullptr;
}

void TranspositionTable::store(uint64_t key, const Move &move, int score, int depth, Bound bound) {
  if (table.empty()) return;

  Cluster &cluster = table[key & mask];
  const uint16_t wanted = key16_of(key);

  Entry *replace = nullptr;
  int replace_value = 0;

  for (Entry &entry : cluster.entries) {
    // Same position: this is the slot to keep current, whatever its depth.
    if (entry.key16 == wanted && entry.bound() != NONE) {
      // A deeper result from this same search already proves more than this
      // one would, so leave it alone.
      if (entry.depth > depth && (entry.gen_bound & GEN_MASK) == generation) return;

      replace = &entry;
      break;
    }

    if (entry.bound() == NONE) {
      replace = &entry;
      break;
    }

    // Depth is what an entry is worth, age is what it has lost since. Two
    // plies per generation behind is enough that a stale deep entry gives way
    // to a current shallow one after a few moves.
    int age = ((GEN_CYCLE + generation - entry.gen_bound) & GEN_MASK) / GEN_DELTA;
    int value = entry.depth - 2 * age;

    if (replace == nullptr || value < replace_value) {
      replace = &entry;
      replace_value = value;
    }
  }

  // A null move carries no ordering hint, so keep whatever is already stored
  // for this position rather than blanking it.
  uint16_t move16 = move.raw();
  if (move16 == 0 && replace->key16 == wanted) move16 = replace->move16;

  *replace = Entry{
      wanted,
      move16,
      static_cast<int16_t>(score),
      static_cast<int8_t>(depth),
      static_cast<uint8_t>(generation | bound),
  };
}
