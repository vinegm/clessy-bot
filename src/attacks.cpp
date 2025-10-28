
#include "attacks.hpp"

uint64_t scan_attacks(int square, uint64_t occupancy, int rank_dir, int file_dir) {
  uint64_t attacks = 0ULL;
  int piece_rank = square_rank(square);
  int piece_file = square_file(square);

  int rank = piece_rank + rank_dir;
  int file = piece_file + file_dir;
  while (rank >= 0 && rank < 8 && file >= 0 && file < 8) {
    uint64_t bit = 1ULL << (rank * 8 + file);
    attacks |= bit;

    if (occupancy & bit) break;
    rank += rank_dir;
    file += file_dir;
  }

  return attacks;
}

uint64_t get_rook_attacks(int square, uint64_t occupancy) {
  uint64_t attacks = 0ULL;
  attacks |= scan_attacks(square, occupancy, 0, -1);
  attacks |= scan_attacks(square, occupancy, 0, 1);
  attacks |= scan_attacks(square, occupancy, -1, 0);
  attacks |= scan_attacks(square, occupancy, 1, 0);
  return attacks;
}

uint64_t get_bishop_attacks(int square, uint64_t occupancy) {
  uint64_t attacks = 0ULL;
  attacks |= scan_attacks(square, occupancy, 1, 1);
  attacks |= scan_attacks(square, occupancy, 1, -1);
  attacks |= scan_attacks(square, occupancy, -1, 1);
  attacks |= scan_attacks(square, occupancy, -1, -1);
  return attacks;
}
