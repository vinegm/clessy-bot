#include "transposition_table.hpp"

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

void TranspositionTable::new_search() { generation += GENERATION_STEP; }

size_t TranspositionTable::hashfull() {
  if (table.empty()) return 0;

  // NOTE: sampled, not counted, it's only ever displayed
  size_t sampled = 0;
  size_t used = 0;
  for (size_t i = 0; i < table.size() && sampled < 1000; i++) {
    for (const Entry &entry : table[i].entries) {
      if (sampled++ >= 1000) break;
      if (entry.bound() == NONE) continue;
      if ((entry.generation_bound & GENERATION_MASK) != generation) continue;

      used++;
    }
  }

  if (sampled == 0) return 0;

  return used * 1000 / sampled;
}

const TranspositionTable::Entry *TranspositionTable::probe(uint64_t key) {
  if (table.empty()) return nullptr;

  const Cluster &cluster = table[key & mask];
  const uint16_t wanted = key_signature_of(key);

  for (const Entry &entry : cluster.entries) {
    if (entry.key_signature == wanted && entry.bound() != NONE) return &entry;
  }

  return nullptr;
}

void TranspositionTable::store(uint64_t key, const Move &move, int score, int depth, Bound bound) {
  if (table.empty()) return;

  Cluster &cluster = table[key & mask];
  const uint16_t wanted = key_signature_of(key);

  Entry *replace = nullptr;
  int replace_value = 0;

  for (Entry &entry : cluster.entries) {
    if (entry.key_signature == wanted && entry.bound() != NONE) {
      bool same_search = (entry.generation_bound & GENERATION_MASK) == generation;
      if (entry.depth > depth && same_search) return;

      replace = &entry;
      break;
    }

    if (entry.bound() == NONE) {
      replace = &entry;
      break;
    }

    // NOTE: two plies per generation lets a stale deep entry give way after a few moves
    int age = ((GENERATION_CYCLE + generation - entry.generation_bound) & GENERATION_MASK)
              / GENERATION_STEP;
    int value = entry.depth - 2 * age;

    if (replace == nullptr || value < replace_value) {
      replace = &entry;
      replace_value = value;
    }
  }

  // NOTE: a null move carries no ordering hint, so keep the stored one
  uint16_t packed_move = move.raw();
  if (packed_move == 0 && replace->key_signature == wanted) packed_move = replace->packed_move;

  *replace = Entry{
      wanted,
      packed_move,
      static_cast<int16_t>(score),
      static_cast<int8_t>(depth),
      static_cast<uint8_t>(generation | bound),
  };
}
