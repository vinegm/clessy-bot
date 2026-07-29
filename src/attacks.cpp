#include "attacks.hpp"

#include <cstring>

MagicEntry rook_magics[64];
MagicEntry bishop_magics[64];

// Flat backing tables, sized by the standard sums of 2^popcount(mask[square]).
static uint64_t rook_table[102400];
static uint64_t bishop_table[5248];

// Every attack a slider has from a square, given what blocks it. Slow, so it
// is only ever used to fill the tables at startup.
using SlowAttacks = uint64_t (*)(int square, uint64_t occupancy);

static uint64_t scan_ray(int square, uint64_t occupancy, int rank_dir, int file_dir) {
  uint64_t attacks = 0;

  int rank = square_rank(square) + rank_dir;
  int file = square_file(square) + file_dir;

  while (rank >= 0 && rank < 8 && file >= 0 && file < 8) {
    uint64_t bit = 1ULL << (rank * 8 + file);
    attacks |= bit;

    if (occupancy & bit) break;

    rank += rank_dir;
    file += file_dir;
  }

  return attacks;
}

static uint64_t slow_rook_attacks(int square, uint64_t occupancy) {
  return scan_ray(square, occupancy, 0, 1) | scan_ray(square, occupancy, 0, -1)
         | scan_ray(square, occupancy, 1, 0) | scan_ray(square, occupancy, -1, 0);
}

static uint64_t slow_bishop_attacks(int square, uint64_t occupancy) {
  return scan_ray(square, occupancy, 1, 1) | scan_ray(square, occupancy, 1, -1)
         | scan_ray(square, occupancy, -1, 1) | scan_ray(square, occupancy, -1, -1);
}

// The squares whose occupancy changes what a rook on this square attacks. The
// edges are left out: a blocker there stops the ray either way.
static uint64_t rook_mask(int square) {
  uint64_t mask = 0;
  int rank = square_rank(square);
  int file = square_file(square);

  for (int i = 1; i <= 6; i++) {
    if (i != rank) mask |= 1ULL << (i * 8 + file);
    if (i != file) mask |= 1ULL << (rank * 8 + i);
  }

  return mask;
}

static uint64_t bishop_mask(int square) {
  uint64_t mask = 0;
  int rank = square_rank(square);
  int file = square_file(square);

  for (int rank_dir : {1, -1}) {
    for (int file_dir : {1, -1}) {
      int current_rank = rank + rank_dir;
      int current_file = file + file_dir;

      while (current_rank >= 1 && current_rank <= 6 && current_file >= 1 && current_file <= 6) {
        mask |= 1ULL << (current_rank * 8 + current_file);
        current_rank += rank_dir;
        current_file += file_dir;
      }
    }
  }

  return mask;
}

#ifndef __BMI2__
// Magic-number finder, only needed where PEXT cannot do the indexing.
static uint64_t rng_state = 0x98F4BC3A17E52D6FULL;

static uint64_t next_rand() {
  rng_state ^= rng_state >> 12;
  rng_state ^= rng_state << 25;
  rng_state ^= rng_state >> 27;
  return rng_state * 0x2545F4914F6CDD1DULL;
}

// Few bits set means fewer index collisions, so a sparse candidate is far
// likelier to work out than a uniform one.
static uint64_t sparse_rand() { return next_rand() & next_rand() & next_rand(); }

static void find_magic(MagicEntry &entry, uint64_t *table, int square, SlowAttacks slow) {
  const uint64_t mask = entry.mask;
  const int bits = count_bits(mask);
  const int shift = 64 - bits;
  const int size = 1 << bits;

  // Every occupancy subset of the mask, with the attacks it produces.
  static uint64_t subsets[4096];
  static uint64_t attacks[4096];
  int count = 0;

  uint64_t subset = mask;
  while (true) {
    subsets[count] = subset;
    attacks[count] = slow(square, subset);
    count++;

    if (subset == 0) break;

    subset = (subset - 1) & mask;
  }

  // A magic works when every subset sharing an index also shares its attacks,
  // so a collision is only fatal when the two disagree.
  static uint64_t used[4096];
  while (true) {
    uint64_t magic = sparse_rand();
    if (count_bits((mask * magic) >> 56) < 6) continue;

    memset(used, 0, size * sizeof(uint64_t));

    bool works = true;
    for (int i = 0; i < count && works; i++) {
      int index = ((subsets[i] & mask) * magic) >> shift;

      if (used[index] == 0) {
        used[index] = attacks[i];
        continue;
      }

      works = used[index] == attacks[i];
    }

    if (!works) continue;

    entry.magic = magic;
    entry.shift = shift;
    memcpy(table, used, size * sizeof(uint64_t));

    return;
  }
}
#endif // !__BMI2__

static void
    init_magic(MagicEntry &entry, uint64_t mask, uint64_t *table, int square, SlowAttacks slow) {
  entry.mask = mask;
  entry.attacks = table;

#ifdef __BMI2__
  // PEXT indexes by the extracted occupancy, so there is no magic to look for:
  // fill the entry straight from every subset of the mask.
  uint64_t subset = mask;
  while (true) {
    entry.attacks[_pext_u64(subset, mask)] = slow(square, subset);

    if (subset == 0) break;

    subset = (subset - 1) & mask;
  }
#else
  find_magic(entry, table, square, slow);
#endif
}

void init_attack_tables() {
  int rook_offset = 0;
  int bishop_offset = 0;

  for (int square = 0; square < 64; square++) {
    uint64_t rook_occupancy = rook_mask(square);
    init_magic(
        rook_magics[square],
        rook_occupancy,
        rook_table + rook_offset,
        square,
        slow_rook_attacks
    );
    rook_offset += 1 << count_bits(rook_occupancy);

    uint64_t bishop_occupancy = bishop_mask(square);
    init_magic(
        bishop_magics[square],
        bishop_occupancy,
        bishop_table + bishop_offset,
        square,
        slow_bishop_attacks
    );
    bishop_offset += 1 << count_bits(bishop_occupancy);
  }
}
