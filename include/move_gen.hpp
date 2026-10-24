#pragma once

#include "chess_types.hpp"

#include <cstdint>

class Position;

class MoveGenerator {
public:
  MoveGenerator() = delete;

  // NOTE: one node's checks and pins, so staged generation computes them once
  struct Masks {
    uint64_t check_mask = 0;
    uint64_t checkers = 0;

    uint64_t pinned_pieces = 0;
    uint64_t pin_rays[64] = {}; // NOTE: only set for squares in pinned_pieces
  };

  static MoveList generate_legal_moves(const Position &position);
  static MoveList generate_legal_captures(const Position &position);

  // Staged generation: noisy and quiet split the legal moves between them
  static Masks generate_masks(const Position &position);
  static MoveList generate_noisy(const Position &position, const Masks &masks);
  static MoveList generate_quiets(const Position &position, const Masks &masks);
  static bool is_legal(const Position &position, const Masks &masks, const Move &move);

  static bool is_in_check(const Position &position, Color color);

private:
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
  static bool is_square_attacked(
      const Position &position,
      Square square,
      Color by_color,
      uint64_t occupancy
  );
  static uint64_t safe_king_squares(const Position &position, uint64_t squares);
  static Square find_king(const Position &position, Color color);
};
