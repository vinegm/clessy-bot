#pragma once

#include "chess_types.hpp"

#include <array>
#include <cassert>
#include <cstdint>

#ifdef __BMI2__
#include <immintrin.h>
#endif

constexpr std::array<std::array<uint64_t, 64>, 2> init_pawn_attacks() {
  std::array<std::array<uint64_t, 64>, 2> pawn_attacks{}; // [Color][Square]
  const uint64_t NOT_FILE_A = ~FILE_A;
  const uint64_t NOT_FILE_H = ~FILE_H;

  for (int square = 0; square < 64; square++) {
    uint64_t square_bit = 1ULL << square;
    uint64_t white_attacks = 0ULL;
    uint64_t black_attacks = 0ULL;

    if (square_bit & NOT_FILE_A) white_attacks |= square_bit << 7;
    if (square_bit & NOT_FILE_H) white_attacks |= square_bit << 9;

    if (square_bit & NOT_FILE_A) black_attacks |= square_bit >> 9;
    if (square_bit & NOT_FILE_H) black_attacks |= square_bit >> 7;

    pawn_attacks[0][square] = white_attacks;
    pawn_attacks[1][square] = black_attacks;
  }

  return pawn_attacks;
}

constexpr std::array<uint64_t, 64> init_knight_attacks() {
  std::array<uint64_t, 64> knight_attacks{};
  const uint64_t NOT_FILE_A = ~FILE_A;
  const uint64_t NOT_FILE_B = ~FILE_B;
  const uint64_t NOT_FILE_G = ~FILE_G;
  const uint64_t NOT_FILE_H = ~FILE_H;

  for (int square = 0; square < 64; square++) {
    uint64_t attacks = 0ULL;
    uint64_t bit = 1ULL << square;

    if (bit & NOT_FILE_A) attacks |= bit << 15 | bit >> 17;
    if (bit & NOT_FILE_H) attacks |= bit << 17 | bit >> 15;
    if (bit & NOT_FILE_A & NOT_FILE_B) attacks |= bit << 6 | bit >> 10;
    if (bit & NOT_FILE_G & NOT_FILE_H) attacks |= bit << 10 | bit >> 6;

    knight_attacks[square] = attacks;
  }

  return knight_attacks;
}

constexpr std::array<uint64_t, 64> init_king_attacks() {
  std::array<uint64_t, 64> king_attacks{};
  const uint64_t NOT_FILE_A = ~FILE_A;
  const uint64_t NOT_FILE_H = ~FILE_H;

  for (int square = 0; square < 64; square++) {
    uint64_t attacks = 0ULL;
    uint64_t square_bit = 1ULL << square;

    if (square_bit & NOT_FILE_A) attacks |= square_bit >> 1;
    if (square_bit & NOT_FILE_H) attacks |= square_bit << 1;
    if (square_bit & NOT_FILE_A) attacks |= (square_bit << 8) >> 1 | (square_bit >> 8) >> 1;
    if (square_bit & NOT_FILE_H) attacks |= (square_bit << 8) << 1 | (square_bit >> 8) << 1;
    attacks |= (square_bit << 8) | (square_bit >> 8);

    king_attacks[square] = attacks;
  }

  return king_attacks;
}

constexpr std::array<CastlingRights, 64> init_castling_masks() {
  std::array<CastlingRights, 64> castling_masks{};

  for (int square = 0; square < 64; square++) {
    castling_masks[square] = ANY_CASTLING;

    switch (square) {
      case E1: castling_masks[square] &= ~(W_CASTLE_KING | W_CASTLE_QUEEN); break;
      case H1: castling_masks[square] &= ~W_CASTLE_KING; break;
      case A1: castling_masks[square] &= ~W_CASTLE_QUEEN; break;
      case E8: castling_masks[square] &= ~(B_CASTLE_KING | B_CASTLE_QUEEN); break;
      case H8: castling_masks[square] &= ~B_CASTLE_KING; break;
      case A8: castling_masks[square] &= ~B_CASTLE_QUEEN; break;
      default: break;
    }
  }

  return castling_masks;
}

constexpr const auto PAWN_ATTACKS = init_pawn_attacks();
constexpr const auto KNIGHT_ATTACKS = init_knight_attacks();
constexpr const auto KING_ATTACKS = init_king_attacks();
constexpr const auto CASTLING_MASKS = init_castling_masks();

// -------------------- Sliding pieces --------------------
// NOTE: uses magic bitboards to compute attacks for sliding pieces (rooks and bishops).
// uses PEXT instruction if available, otherwise uses magic multiplication and shift.

struct MagicEntry {
  uint64_t mask;
#ifndef __BMI2__
  uint64_t magic;
  int shift;
#endif
  uint64_t *attacks;
};

extern MagicEntry rook_magics[64];
extern MagicEntry bishop_magics[64];

void init_attack_tables();

inline uint64_t get_rook_attacks(int square, uint64_t occupancy) {
#ifdef __BMI2__
  const MagicEntry &magic = rook_magics[square];
  return magic.attacks[_pext_u64(occupancy, magic.mask)];
#else
  const MagicEntry &magic = rook_magics[square];
  return magic.attacks[((occupancy & magic.mask) * magic.magic) >> magic.shift];
#endif
}

inline uint64_t get_bishop_attacks(int square, uint64_t occupancy) {
#ifdef __BMI2__
  const MagicEntry &magic = bishop_magics[square];
  return magic.attacks[_pext_u64(occupancy, magic.mask)];
#else
  const MagicEntry &magic = bishop_magics[square];
  return magic.attacks[((occupancy & magic.mask) * magic.magic) >> magic.shift];
#endif
}
