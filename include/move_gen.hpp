#pragma once

#include "attacks.hpp"
#include "chess_types.hpp"
#include "position.hpp"

#include <array>
#include <cstdint>

struct MoveList {
  Move moves[MAX_POSSIBLE_LEGAL_MOVES];
  int count = 0;

  void add_move(const Move &move) { moves[count++] = move; }

  void add_move(
      Square from,
      Square to,
      MoveType type = NORMAL_MOVE,
      PieceType promotion = PIECE_NONE
  ) {
    moves[count++] = {from, to, type, promotion};
  }

  Move *begin() { return moves; }
  Move *end() { return moves + count; }
  const Move *begin() const { return moves; }
  const Move *end() const { return moves + count; }

  bool empty() const { return count == 0; }
  int size() const { return count; }
  Move &operator[](int index) { return moves[index]; }
  const Move &operator[](int index) const { return moves[index]; }
};

class MoveGenerator {
public:
  MoveList generate_pseudo_legal_moves(const Position &position) const;
  MoveList generate_legal_moves(const Position &position) const;

private:
  template<PieceColor Us>
  int generate_pawn_moves(const Position &position, Move *moves) const;

  template<PieceType PieceT>
  int generate_piece_moves(const Position &position, Move *moves) const;

  int generate_castling_moves(const Position &position, Move *moves) const;

  bool is_square_attacked(const Position &position, Square square, PieceColor by_color) const;
  bool is_in_check(const Position &position, PieceColor color) const;
  bool is_legal_move(const Position &position, const Move &move) const;
  Square find_king(const Position &position, PieceColor color) const;

  const std::array<std::array<uint64_t, 64>, 2> PAWN_ATTACKS = init_pawn_attacks();
  const std::array<uint64_t, 64> KNIGHT_ATTACKS = init_knight_attacks();
  const std::array<uint64_t, 64> KING_ATTACKS = init_king_attacks();
};
