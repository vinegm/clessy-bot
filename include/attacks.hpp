#pragma once

#include "chess_types.hpp"

#include <array>
#include <cassert>
#include <cstdint>

// NOTE: CLESSY_NO_PEXT (cmake -DCLESSY_PEXT=OFF) falls back to magic multiplication on
// a CPU that has BMI2 but a slow pext, Zen 1 and 2 microcode it. Measured on a Zen 2
// it saved under 1%, as lookups wait on the tables more than on pext, and the magics
// are searched at startup, about 0.5s, so it stays opt-in
#if defined(__BMI2__) && !defined(CLESSY_NO_PEXT)
#define CLESSY_USE_PEXT
#include <immintrin.h>
#endif

// NOTE: uses magic bitboards to compute attacks for sliding pieces.
struct MagicEntry {
  uint64_t mask;
#ifndef CLESSY_USE_PEXT
  uint64_t magic;
  int shift;
#endif
  uint64_t *attacks;
};

class Attacks {
public:
  Attacks() = delete;

  static uint64_t pawn_attacks(Color color, int square) { return PAWN_TABLE[color][square]; }
  static uint64_t knight_attacks(int square) { return KNIGHT_TABLE[square]; }
  static uint64_t king_attacks(int square) { return KING_TABLE[square]; }

  static uint64_t rook_attacks(int square, uint64_t occupancy) {
    return lookup(rook_magics[square], occupancy);
  }

  static uint64_t bishop_attacks(int square, uint64_t occupancy) {
    return lookup(bishop_magics[square], occupancy);
  }

private:
  static const std::array<std::array<uint64_t, 64>, 2> PAWN_TABLE; // [Color][Square]
  static const std::array<uint64_t, 64> KNIGHT_TABLE;
  static const std::array<uint64_t, 64> KING_TABLE;

  static constexpr std::array<std::array<uint64_t, 64>, 2> init_pawn_attacks();
  static constexpr std::array<uint64_t, 64> init_knight_attacks();
  static constexpr std::array<uint64_t, 64> init_king_attacks();

  static MagicEntry rook_magics[64];
  static MagicEntry bishop_magics[64];

  // NOTE: sized by the sum of 2^popcount(mask) over every square
  static uint64_t rook_table[102400];
  static uint64_t bishop_table[5248];

  // NOTE: set by building the tables during static initialization, before main runs
  static const bool initialized;

  static bool initialize() noexcept;

  static uint64_t lookup(const MagicEntry &magic, uint64_t occupancy) {
#ifdef CLESSY_USE_PEXT
    return magic.attacks[_pext_u64(occupancy, magic.mask)];
#else
    return magic.attacks[((occupancy & magic.mask) * magic.magic) >> magic.shift];
#endif
  }
};

constexpr std::array<std::array<uint64_t, 64>, 2> Attacks::init_pawn_attacks() {
  std::array<std::array<uint64_t, 64>, 2> pawn_attacks{};
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

constexpr std::array<uint64_t, 64> Attacks::init_knight_attacks() {
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

constexpr std::array<uint64_t, 64> Attacks::init_king_attacks() {
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

inline constexpr std::array<std::array<uint64_t, 64>, 2> Attacks::PAWN_TABLE =
    Attacks::init_pawn_attacks();
inline constexpr std::array<uint64_t, 64> Attacks::KNIGHT_TABLE = Attacks::init_knight_attacks();
inline constexpr std::array<uint64_t, 64> Attacks::KING_TABLE = Attacks::init_king_attacks();
