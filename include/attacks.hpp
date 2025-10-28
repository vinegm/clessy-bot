#pragma once

#include "chess_types.hpp"

#include <array>

constexpr std::array<std::array<uint64_t, 64>, 2> init_pawn_attacks() {
  std::array<std::array<uint64_t, 64>, 2> pawn_attacks{}; // [PieceColor][Square]
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

uint64_t get_rook_attacks(int square, uint64_t occupancy);
uint64_t get_bishop_attacks(int square, uint64_t occupancy);
