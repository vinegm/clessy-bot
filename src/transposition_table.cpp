#include "transposition_table.hpp"

#include <algorithm>

void TranspositionTable::resize(size_t mb) {
  size_t max_entries = mb * 1024 * 1024 / sizeof(Entry);

  size_t entries = 1;
  while (entries * 2 <= max_entries) {
    entries *= 2;
  }

  table.assign(entries, Entry{});
  mask = entries - 1;
}

void TranspositionTable::clear() { table.assign(table.size(), Entry{}); }

size_t TranspositionTable::hashfull() {
  if (table.empty()) return 0;

  // NOTE: sampled, not counted, it's only ever displayed
  size_t sampled = std::min<size_t>(table.size(), 1000);
  size_t used = 0;
  for (size_t i = 0; i < sampled; i++) {
    if (table[i].bound != NONE) used++;
  }

  return used * 1000 / sampled;
}

const TranspositionTable::Entry *TranspositionTable::probe(uint64_t key) {
  if (table.empty()) return nullptr;

  const Entry &entry = table[key & mask];
  if (entry.key != key || entry.bound == NONE) return nullptr;

  return &entry;
}

void TranspositionTable::store(uint64_t key, const Move &move, int score, int depth, Bound bound) {
  if (table.empty()) return;

  Entry &entry = table[key & mask];
  if (entry.key == key && entry.depth > depth) return;

  entry = {key, move, static_cast<int16_t>(score), static_cast<int8_t>(depth), bound};
}
