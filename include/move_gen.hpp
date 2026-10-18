#pragma once

#include "chess_types.hpp"

#include <cstdint>

class Position;

class MoveGenerator {
public:
  MoveGenerator() = delete;

  static MoveList generate_legal_moves(const Position &position);
  static MoveList generate_legal_captures(const Position &position);

  static bool is_in_check(const Position &position, Color color);

private:
  struct Masks {
    uint64_t enemy_attacks = 0;
    uint64_t check_mask = 0;
    uint64_t checkers = 0;

    uint64_t pinned_pieces = 0;
    uint64_t pin_rays[64] = {}; // NOTE: only set for squares in pinned_pieces
  };

  static MoveList generate_pseudo_legal_moves(const Position &position);

  static uint64_t legal_targets(Square from, const Masks &masks);

  template<PieceType PieceT>
  static uint64_t piece_attacks(Square from, const Position &position, const Masks &masks);

  using SliderAttacks = uint64_t (*)(int square, uint64_t occupancy);

  static MoveList filter_en_passant_legality(const Position &position, const MoveList &pseudo);
  static int
      generate_moves(const Position &position, Move *moves, const Masks &masks, uint64_t targets);

  template<Color Us>
  static int generate_pawn_moves(
      const Position &position,
      Move *moves,
      const Masks &masks,
      uint64_t targets
  );

  template<PieceType PieceT>
  static int generate_piece_moves(
      const Position &position,
      Move *moves,
      const Masks &masks,
      uint64_t targets
  );

  static int generate_castling_moves(const Position &position, Move *moves, const Masks &masks);

  static Masks generate_masks(const Position &position);
  static uint64_t attack_map(const Position &position, Color color, uint64_t occupancy);
  static void find_checkers(const Position &position, Square king_square, Masks &masks);
  static void find_pins(
      const Position &position,
      Square king_square,
      uint64_t snipers,
      SliderAttacks attacks,
      Masks &masks
  );

  static bool is_legal_move(Position &position, const Move &move);
  static bool is_square_attacked(const Position &position, Square square, Color by_color);
  static Square find_king(const Position &position, Color color);
};
