#pragma once

#include "chess_types.hpp"

#include <cstdint>

constexpr uint64_t splitmix64(uint64_t &state) {
  state += 0x9e3779b97f4a7c15ULL;

  uint64_t z = state;

  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;

  return z ^ (z >> 31);
}

struct ZobristKeys {
  uint64_t piece_square[PIECE_NB][64]{}; // [Piece][Square]
  uint64_t castling[16]{};               // [CastlingRights]
  uint64_t en_passant_file[8]{};
  uint64_t side_to_move{};
};

constexpr ZobristKeys generate_zobrist_keys() {
  ZobristKeys keys{};
  uint64_t state = 0xc1e55e55b0a7dULL;

  for (int piece = 0; piece < PIECE_NB; piece++) {
    for (int sq = 0; sq < 64; sq++) {
      keys.piece_square[piece][sq] = splitmix64(state);
    }
  }

  for (int i = 0; i < 16; i++) {
    keys.castling[i] = splitmix64(state);
  }

  for (int file = 0; file < 8; file++) {
    keys.en_passant_file[file] = splitmix64(state);
  }

  keys.side_to_move = splitmix64(state);

  return keys;
}

inline constexpr ZobristKeys ZOBRIST_KEYS = generate_zobrist_keys();

constexpr uint64_t zobrist_piece_key(Piece piece, Square square) {
  return ZOBRIST_KEYS.piece_square[piece][square];
}
