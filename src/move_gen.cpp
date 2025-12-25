#include "move_gen.hpp"

#include "chess_types.hpp"
#include "position.hpp"

#include <cstdint>
#include <sys/types.h>

MoveList MoveGenerator::generate_pseudo_legal_moves(const Position &position) const {
  MoveList move_list;
  Move *moves = move_list.moves;
  Move *start = moves;

  Masks masks = generate_masks(position);

  if (position.to_move == WHITE) {
    moves += generate_pawn_moves<WHITE>(position, moves, masks);
  } else {
    moves += generate_pawn_moves<BLACK>(position, moves, masks);
  }

  moves += generate_piece_moves<KNIGHT>(position, moves, masks);
  moves += generate_piece_moves<BISHOP>(position, moves, masks);
  moves += generate_piece_moves<ROOK>(position, moves, masks);
  moves += generate_piece_moves<QUEEN>(position, moves, masks);
  moves += generate_piece_moves<KING>(position, moves, masks);
  moves += generate_castling_moves(position, moves, masks);

  move_list.count = moves - start;
  return move_list;
}

MoveList MoveGenerator::generate_legal_moves(const Position &position) const {
  MoveList pseudo_legal = generate_pseudo_legal_moves(position);

  Position temp_position = position;

  MoveList legal_moves;
  for (int i = 0; i < pseudo_legal.count; i++) {
    if (is_legal_move(temp_position, pseudo_legal[i])) { legal_moves.add_move(pseudo_legal[i]); }
  }

  return legal_moves;
}

template<Color Us>
int MoveGenerator::generate_pawn_moves(const Position &position, Move *moves, Masks masks) const {
  int num_checks = count_bits(masks.checkers);
  if (num_checks > 2) return 0;

  Move *start = moves;
  constexpr Color Them = (Us == WHITE) ? BLACK : WHITE;
  constexpr int Forward = (Us == WHITE) ? NORTH : SOUTH;
  constexpr uint64_t StartingRank = (Us == WHITE) ? RANK_2 : RANK_7;
  constexpr uint64_t PromotionRank = (Us == WHITE) ? RANK_8 : RANK_1;

  const uint64_t our_pawns = position.get_bb(Us, PAWN);
  const uint64_t enemy_pieces = position.occupancy[Them];
  const uint64_t empty_squares = ~position.occupancy[ANY_COLOR];

  // Single pushes
  uint64_t single_pushes;
  if constexpr (Us == WHITE) {
    single_pushes = (our_pawns << NORTH) & empty_squares;
  } else {
    single_pushes = (our_pawns >> (-SOUTH)) & empty_squares;
  }

  while (single_pushes) {
    const Square to = pop_lsb_square(single_pushes);
    const Square from = static_cast<Square>(to - Forward);

    if (square_to_bit(to) & PromotionRank) {
      *moves++ = {from, to, PROMOTION, QUEEN};
      *moves++ = {from, to, PROMOTION, ROOK};
      *moves++ = {from, to, PROMOTION, BISHOP};
      *moves++ = {from, to, PROMOTION, KNIGHT};
    } else {
      *moves++ = {from, to};
    }
  }

  // Double pushes
  uint64_t double_pushes;
  if constexpr (Us == WHITE) {
    const uint64_t single_push_from_start = ((our_pawns & StartingRank) << NORTH) & empty_squares;
    double_pushes = (single_push_from_start << NORTH) & empty_squares;
  } else {
    const uint64_t single_push_from_start =
        ((our_pawns & StartingRank) >> (-SOUTH)) & empty_squares;
    double_pushes = (single_push_from_start >> (-SOUTH)) & empty_squares;
  }

  while (double_pushes) {
    const Square to = pop_lsb_square(double_pushes);
    const Square from = static_cast<Square>(to - 2 * Forward);
    *moves++ = {from, to};
  }

  // Regular captures
  uint64_t pawns_copy = our_pawns;
  while (pawns_copy) {
    const Square from = pop_lsb_square(pawns_copy);
    uint64_t attacks = PAWN_ATTACKS[Us][from] & enemy_pieces;

    if (num_checks == 1) attacks &= masks.check_mask | masks.checkers;

    while (attacks) {
      const Square to = pop_lsb_square(attacks);

      if ((square_to_bit(to) & PromotionRank) == 0) {
        *moves++ = {from, to, CAPTURE};
        continue;
      }

      *moves++ = {from, to, static_cast<MoveType>(CAPTURE | PROMOTION), QUEEN};
      *moves++ = {from, to, static_cast<MoveType>(CAPTURE | PROMOTION), ROOK};
      *moves++ = {from, to, static_cast<MoveType>(CAPTURE | PROMOTION), BISHOP};
      *moves++ = {from, to, static_cast<MoveType>(CAPTURE | PROMOTION), KNIGHT};
    }
  }

  // En passant captures
  if (position.en_passant_square.has_value()) {
    const Square en_passant_square = position.en_passant_square.value();
    pawns_copy = our_pawns;

    while (pawns_copy) {
      const Square from = pop_lsb_square(pawns_copy);
      if (PAWN_ATTACKS[Us][from] & square_to_bit(en_passant_square)) {
        *moves++ = {from, en_passant_square, EN_PASSANT};
      }
    }
  }

  return moves - start;
}

template<PieceType PieceT>
int MoveGenerator::generate_piece_moves(const Position &position, Move *moves, Masks masks) const {
  int num_checks = count_bits(masks.checkers);
  if (num_checks > 2) return 0;

  Move *start = moves;
  const Color us = position.to_move;
  const Color them = opposite_color(us);

  uint64_t our_pieces_bb = position.get_bb(us, PieceT);
  const uint64_t our_occupancy = position.occupancy[us];
  const uint64_t enemy_occupancy = position.occupancy[them];

  while (our_pieces_bb) {
    const Square from = pop_lsb_square(our_pieces_bb);
    uint64_t attacks;

    if constexpr (PieceT == KNIGHT) {
      attacks = KNIGHT_ATTACKS[from];
    } else if constexpr (PieceT == BISHOP) {
      attacks = get_bishop_attacks(from, position.occupancy[ANY_COLOR]);
    } else if constexpr (PieceT == ROOK) {
      attacks = get_rook_attacks(from, position.occupancy[ANY_COLOR]);
    } else if constexpr (PieceT == QUEEN) {
      attacks = get_rook_attacks(from, position.occupancy[ANY_COLOR])
                | get_bishop_attacks(from, position.occupancy[ANY_COLOR]);
    } else if constexpr (PieceT == KING) {
      attacks = KING_ATTACKS[from] & ~masks.enemy_attacks;
    }

    attacks &= ~our_occupancy;
    if constexpr (PieceT != KING) {
      if (num_checks == 1) attacks &= masks.check_mask | masks.checkers;
    }

    while (attacks) {
      const Square to = pop_lsb_square(attacks);
      const MoveType move_type = (square_to_bit(to) & enemy_occupancy) ? CAPTURE : NORMAL_MOVE;

      *moves++ = {from, to, move_type};
    }
  }

  return moves - start;
}

int MoveGenerator::generate_castling_moves(
    const Position &position,
    Move *moves,
    Masks masks
) const {
  Move *start = moves;
  const Color us = position.to_move;
  const Color them = opposite_color(us);

  if (position.castling_rights == 0) return 0;
  if (masks.checkers) return 0;

  const uint64_t king_bb = position.get_bb(us, KING);
  const Square king_square = static_cast<Square>(lsb_square(king_bb));

  // King-side castling
  if ((us == WHITE && (position.castling_rights & WHITE_CASTLE_KING))
      || (us == BLACK && (position.castling_rights & BLACK_CASTLE_KING))) {
    const uint64_t destination_bb = move_bit<EAST>(king_bb, 2);
    const uint64_t middle_bb = move_bit<EAST>(king_bb, 1);

    const uint64_t between_squares = middle_bb | destination_bb;
    const uint64_t king_path = king_bb | between_squares;

    if (!(position.occupancy[ANY_COLOR] & between_squares) && !(masks.enemy_attacks & king_path)) {
      const Square king_dest = static_cast<Square>(lsb_square(destination_bb));
      *moves++ = {king_square, king_dest, CASTLING};
    }
  }

  // Queen-side castling
  if ((us == WHITE && (position.castling_rights & WHITE_CASTLE_QUEEN))
      || (us == BLACK && (position.castling_rights & BLACK_CASTLE_QUEEN))) {
    const uint64_t destination_bb = move_bit<WEST>(king_bb, 2);
    const uint64_t middle_bb = move_bit<WEST>(king_bb, 1);
    const uint64_t outer_bb = move_bit<WEST>(king_bb, 3);

    const uint64_t between_squares = middle_bb | destination_bb | outer_bb;
    const uint64_t king_path = king_bb | middle_bb | destination_bb;

    if (!(position.occupancy[ANY_COLOR] & between_squares) && !(masks.enemy_attacks & king_path)) {
      const Square king_dest = static_cast<Square>(lsb_square(destination_bb));
      *moves++ = {king_square, king_dest, CASTLING};
    }
  }

  return moves - start;
}

Masks MoveGenerator::generate_masks(const Position &position) const {
  const Color us = position.to_move;
  const Color them = opposite_color(us);
  const uint64_t our_king_bb = position.get_bb(us, KING);
  const Square king_square = static_cast<Square>(lsb_square(our_king_bb));
  Masks masks;

  auto add_piece_attacks = [&](PieceType piece_type, auto attack_func) {
    uint64_t pieces = position.get_bb(them, piece_type);
    while (pieces) {
      const Square from = pop_lsb_square(pieces);
      const uint64_t from_bb = square_to_bit(from);

      uint64_t attacks = attack_func(from);
      masks.enemy_attacks |= attacks;

      if (attacks & our_king_bb) {
        masks.checkers |= from_bb;
        // TODO: For sliding pieces only append the ray that checks the king
        masks.check_mask |= attacks;
      }
    }
  };

  add_piece_attacks(PAWN, [&](Square from) { return PAWN_ATTACKS[them][from]; });
  add_piece_attacks(KNIGHT, [&](Square from) { return KNIGHT_ATTACKS[from]; });
  add_piece_attacks(BISHOP, [&](Square from) {
    return get_bishop_attacks(from, position.occupancy[ANY_COLOR]);
  });
  add_piece_attacks(ROOK, [&](Square from) {
    return get_rook_attacks(from, position.occupancy[ANY_COLOR]);
  });
  add_piece_attacks(QUEEN, [&](Square from) {
    return get_rook_attacks(from, position.occupancy[ANY_COLOR])
           | get_bishop_attacks(from, position.occupancy[ANY_COLOR]);
  });

  uint64_t enemy_king = position.get_bb(them, KING);
  if (enemy_king) {
    const Square from = static_cast<Square>(lsb_square(enemy_king));
    masks.enemy_attacks |= KING_ATTACKS[from];
  }

  return masks;
}

bool MoveGenerator::is_square_attacked(
    const Position &position,
    Square square,
    Color enemy_color
) const {
  const uint64_t all_pieces = position.occupancy[ANY_COLOR];

  if (PAWN_ATTACKS[opposite_color(enemy_color)][square] & position.get_bb(enemy_color, PAWN)) {
    return true;
  }

  if (KNIGHT_ATTACKS[square] & position.get_bb(enemy_color, KNIGHT)) { return true; }

  const uint64_t diagonal_attackers =
      position.get_bb(enemy_color, BISHOP) | position.get_bb(enemy_color, QUEEN);
  if (diagonal_attackers && (get_bishop_attacks(square, all_pieces) & diagonal_attackers)) {
    return true;
  }

  const uint64_t straight_attackers =
      position.get_bb(enemy_color, ROOK) | position.get_bb(enemy_color, QUEEN);
  if (straight_attackers && (get_rook_attacks(square, all_pieces) & straight_attackers)) {
    return true;
  }

  if (KING_ATTACKS[square] & position.get_bb(enemy_color, KING)) { return true; }

  return false;
}

bool MoveGenerator::is_in_check(const Position &position, Color color) const {
  Square king_square = find_king(position, color);
  return is_square_attacked(position, king_square, opposite_color(color));
}

bool MoveGenerator::is_legal_move(Position &position, const Move &move) const {
  Color original_to_move = position.to_move;

  position.make_move(move);
  bool check_res = is_in_check(position, original_to_move);
  position.undo_move();

  return !check_res;
}

Square MoveGenerator::find_king(const Position &position, Color color) const {
  const uint64_t king = position.get_bb(color, KING);
  return static_cast<Square>(lsb_square(king));
}
