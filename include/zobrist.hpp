#pragma once

#include "chess_types.hpp"

#include <cstdint>
#include <optional>

class Zobrist {
public:
  Zobrist() = delete;

  static uint64_t piece_key(Piece piece, Square square) { return KEYS.piece_square[piece][square]; }
  static uint64_t castling_key(CastlingRights rights) { return KEYS.castling[rights]; }
  static uint64_t side_to_move_key() { return KEYS.side_to_move; }

  static uint64_t en_passant_key(const std::optional<Square> &square) {
    if (!square.has_value()) return 0;

    return KEYS.en_passant_file[square_file(*square)];
  }

private:
  struct Keys {
    uint64_t piece_square[PIECE_NB][64]{}; // [Piece][Square]
    uint64_t castling[16]{};               // [CastlingRights]
    uint64_t en_passant_file[8]{};
    uint64_t side_to_move{};
  };

  static const Keys KEYS;

  static constexpr uint64_t splitmix64(uint64_t &state);
  static constexpr Keys generate();
};

constexpr uint64_t Zobrist::splitmix64(uint64_t &state) {
  state += 0x9e3779b97f4a7c15ULL;

  uint64_t z = state;

  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;

  return z ^ (z >> 31);
}

constexpr Zobrist::Keys Zobrist::generate() {
  Keys keys{};
  uint64_t state = 0xc1e55e55b0a7dULL;

  for (auto &square_keys : keys.piece_square) {
    for (uint64_t &key : square_keys) {
      key = splitmix64(state);
    }
  }

  for (uint64_t &key : keys.castling) {
    key = splitmix64(state);
  }

  for (uint64_t &key : keys.en_passant_file) {
    key = splitmix64(state);
  }

  keys.side_to_move = splitmix64(state);

  return keys;
}

inline constexpr Zobrist::Keys Zobrist::KEYS = Zobrist::generate();
