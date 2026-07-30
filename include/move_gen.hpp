#pragma once

#include "chess_types.hpp"

#include <cstdint>

class Position;

struct MoveList {
  Move moves[MAX_POSSIBLE_LEGAL_MOVES];
  int count = 0;

  void add_move(const Move &move) { moves[count++] = move; }

  void
      add_move(Square from, Square to, MoveType type = NORMAL_MOVE, PieceType promotion = NO_TYPE) {
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

struct Masks {
  uint64_t enemy_attacks = 0;
  uint64_t check_mask = 0; // between the king and a checking slider
  uint64_t checkers = 0;   // pieces giving check

  // Where a piece that is pinned may still go, capturing the pinner included.
  // Only the squares set in pinned_pieces carry a meaningful ray.
  uint64_t pinned_pieces = 0;
  uint64_t pin_rays[64] = {};
};

class MoveGenerator {
public:
  MoveList generate_legal_moves(const Position &position) const;
  MoveList generate_pseudo_legal_moves(const Position &position) const;

  // Legal captures, en passant and promotions only, for quiescence.
  MoveList generate_legal_captures(const Position &position) const;

  bool is_in_check(const Position &position, Color color) const;

private:
  // Sliding attacks as a function, so the pin search can run once per
  // direction instead of twice over the same code.
  using SliderAttacks = uint64_t (*)(int square, uint64_t occupancy);

  MoveList filter_en_passant_legality(const Position &position, const MoveList &pseudo) const;

  // Everything but castling, which is the part both entry points share.
  // targets restricts where a move may land; pass ~0 for all of them.
  int generate_moves(
      const Position &position,
      Move *moves,
      const Masks &masks,
      uint64_t targets
  ) const;

  template<Color Us>
  int generate_pawn_moves(
      const Position &position,
      Move *moves,
      const Masks &masks,
      uint64_t targets
  ) const;

  template<PieceType PieceT>
  int generate_piece_moves(
      const Position &position,
      Move *moves,
      const Masks &masks,
      uint64_t targets
  ) const;

  int generate_castling_moves(const Position &position, Move *moves, const Masks &masks) const;

  Masks generate_masks(const Position &position) const;
  uint64_t attack_map(const Position &position, Color color, uint64_t occupancy) const;
  void find_checkers(const Position &position, Square king_square, Masks &masks) const;
  void find_pins(
      const Position &position,
      Square king_square,
      uint64_t snipers,
      SliderAttacks attacks,
      Masks &masks
  ) const;

  bool is_legal_move(Position &position, const Move &move) const;
  bool is_square_attacked(const Position &position, Square square, Color by_color) const;
  Square find_king(const Position &position, Color color) const;
};
